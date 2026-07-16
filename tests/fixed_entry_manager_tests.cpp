#include "fixed_entry_manager.h"
#include "tab_group_model.h"
#include "v2_taskbar_readiness.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] HWND TestHandle(const std::uintptr_t value) noexcept {
    return reinterpret_cast<HWND>(value);
}

[[nodiscard]] std::uintptr_t HandleValue(const HWND hwnd) noexcept {
    return reinterpret_cast<std::uintptr_t>(hwnd);
}

[[nodiscard]] ctm::ChromeWindowSnapshot MakeWindow(
    const std::uintptr_t handle,
    const DWORD process_id = 100,
    const DWORD thread_id = 200) {
    ctm::ChromeWindowSnapshot snapshot;
    snapshot.hwnd = TestHandle(handle);
    snapshot.process_id = process_id;
    snapshot.thread_id = thread_id;
    snapshot.process_creation_time =
        static_cast<std::uint64_t>(process_id) * 1000U + 1U;
    snapshot.class_name = L"Chrome_WidgetWin_1";
    snapshot.title = L"Synthetic Chrome window";
    return snapshot;
}

struct FakeCall {
    ctm::FixedEntryOperationKind kind =
        ctm::FixedEntryOperationKind::Remove;
    ctm::WindowIdentity identity;
};

class RecordingRecoveryStore final : public ctm::IRecoveryStateStore {
public:
    [[nodiscard]] bool Save(
        const std::span<const ctm::TaskbarMutationState> states,
        std::wstring* const error_message) override {
        if (event_log != nullptr) {
            event_log->push_back(
                "save:" + std::to_string(states.size()));
        }
        ++save_attempts;
        if (fail_on_attempt != 0 && save_attempts == fail_on_attempt) {
            if (error_message != nullptr) {
                *error_message = L"Configured recovery-store failure.";
            }
            return false;
        }
        saved_states.emplace_back(states.begin(), states.end());
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    int fail_on_attempt = 0;
    int save_attempts = 0;
    std::vector<std::string>* event_log = nullptr;
    std::vector<std::vector<ctm::TaskbarMutationState>> saved_states;
};

class RecordingReadinessGate final
    : public ctm::IFixedEntryReadinessGate {
public:
    [[nodiscard]] bool ConfirmReadyAfterRecoveryWrite(
        const ctm::WindowIdentity& identity,
        std::wstring* const error_message) override {
        confirmed_handles.push_back(HandleValue(identity.hwnd));
        if (event_log != nullptr) {
            event_log->push_back("gate:confirm");
        }
        if (!ready) {
            if (error_message != nullptr) {
                *error_message = L"Configured readiness rejection.";
            }
            return false;
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    void RecoveryIntentCleared(
        const ctm::WindowIdentity& identity) noexcept override {
        cleared_handles.push_back(HandleValue(identity.hwnd));
        if (event_log != nullptr) {
            event_log->push_back("gate:clear");
        }
    }

    bool ready = true;
    std::vector<std::string>* event_log = nullptr;
    std::vector<std::uintptr_t> confirmed_handles;
    std::vector<std::uintptr_t> cleared_handles;
};

class FakeTaskbarController final
    : public ctm::ITaskbarMutationController {
public:
    [[nodiscard]] ctm::TaskbarOperationResult RemoveWindow(
        const ctm::ChromeWindowSnapshot& snapshot,
        const ctm::TaskbarMethod method,
        ctm::TaskbarMutationState* const state) override {
        ctm::TaskbarOperationResult result;
        if (state == nullptr || method != ctm::TaskbarMethod::TaskbarList) {
            result.win32_error = ERROR_INVALID_PARAMETER;
            result.message = L"The fake controller received invalid input.";
            return result;
        }

        const ctm::WindowIdentity identity = {
            .hwnd = snapshot.hwnd,
            .process_id = snapshot.process_id,
            .thread_id = snapshot.thread_id,
            .process_creation_time = snapshot.process_creation_time,
            .class_name = snapshot.class_name,
        };
        calls.push_back({
            .kind = ctm::FixedEntryOperationKind::Remove,
            .identity = identity,
        });
        if (event_log != nullptr) {
            event_log->push_back("taskbar:remove");
        }

        int& remaining_failures =
            remove_failures_[HandleValue(snapshot.hwnd)];
        if (remaining_failures > 0) {
            --remaining_failures;
            result.hresult = E_FAIL;
            result.message = L"Configured fake removal failure.";
            return result;
        }

        *state = {};
        state->method = ctm::TaskbarMethod::TaskbarList;
        state->identity = identity;
        state->modification_applied = true;
        result.succeeded = true;
        result.state_changed = true;
        result.message = L"Configured fake removal success.";
        return result;
    }

    [[nodiscard]] ctm::TaskbarOperationResult RestoreWindow(
        ctm::TaskbarMutationState* const state) override {
        ctm::TaskbarOperationResult result;
        if (state == nullptr) {
            result.win32_error = ERROR_INVALID_PARAMETER;
            return result;
        }
        if (!state->NeedsRestore()) {
            result.succeeded = true;
            result.skipped = true;
            return result;
        }

        calls.push_back({
            .kind = ctm::FixedEntryOperationKind::Restore,
            .identity = state->identity,
        });
        if (event_log != nullptr) {
            event_log->push_back("taskbar:restore");
        }
        int& remaining_failures =
            restore_failures_[HandleValue(state->identity.hwnd)];
        if (remaining_failures > 0) {
            --remaining_failures;
            result.hresult = E_FAIL;
            result.message = L"Configured fake restoration failure.";
            return result;
        }

        state->restore_completed = true;
        result.succeeded = true;
        result.state_changed = true;
        result.message = L"Configured fake restoration success.";
        return result;
    }

    void FailNextRemovals(const std::uintptr_t handle, const int count) {
        remove_failures_[handle] = count;
    }

    void FailNextRestorations(const std::uintptr_t handle, const int count) {
        restore_failures_[handle] = count;
    }

    [[nodiscard]] std::size_t Count(
        const ctm::FixedEntryOperationKind kind) const {
        std::size_t count = 0;
        for (const FakeCall& call : calls) {
            if (call.kind == kind) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t Count(
        const ctm::FixedEntryOperationKind kind,
        const std::uintptr_t handle) const {
        std::size_t count = 0;
        for (const FakeCall& call : calls) {
            if (call.kind == kind &&
                HandleValue(call.identity.hwnd) == handle) {
                ++count;
            }
        }
        return count;
    }

    std::vector<FakeCall> calls;
    std::vector<std::string>* event_log = nullptr;

private:
    std::unordered_map<std::uintptr_t, int> remove_failures_;
    std::unordered_map<std::uintptr_t, int> restore_failures_;
};

void TestZeroAndOneWindowNeedNoTaskbarCalls() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);

    const std::vector<ctm::ChromeWindowSnapshot> empty;
    const ctm::FixedEntryReport empty_report =
        manager.Synchronize(empty, nullptr);
    Expect(empty_report.succeeded,
           "zero windows should synchronize successfully");
    Expect(!manager.main_entry().has_value(),
           "zero windows should not select a main entry");

    const std::vector windows = {MakeWindow(11)};
    const ctm::FixedEntryReport one_report =
        manager.Synchronize(windows, TestHandle(11));
    Expect(one_report.succeeded,
           "one window should synchronize successfully");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 11,
           "the only window should be the main entry");
    Expect(controller.calls.empty(),
           "zero and one window should not invoke taskbar APIs");

    const ctm::FixedEntryReport restore_report = manager.RestoreAll();
    Expect(restore_report.succeeded && controller.calls.empty(),
           "restoring an unmodified one-window state should be idempotent");
}

void TestThreeWindowsKeepTheForegroundEntryFixed() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector windows = {
        MakeWindow(30),
        MakeWindow(10),
        MakeWindow(20),
    };

    const ctm::FixedEntryReport first =
        manager.Synchronize(windows, TestHandle(20));
    Expect(first.succeeded,
           "three windows should synchronize successfully");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 20,
           "a manageable foreground window should become the fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 2,
           "three windows should remove exactly two entries");
    Expect(manager.removed_window_count() == 2,
           "two successful removals should be tracked");

    const ctm::FixedEntryReport repeated =
        manager.Synchronize(windows, TestHandle(30));
    Expect(repeated.succeeded,
           "a repeated synchronization should succeed");
    Expect(HandleValue(manager.main_entry()->hwnd) == 20,
           "foreground changes should not replace a valid fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 2,
           "a repeated synchronization should not repeat DeleteTab");
    Expect(repeated.already_removed_count == 2,
           "a repeated synchronization should report tracked removals");

    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded && manager.removed_window_count() == 0,
           "restore all should clear successful removal state");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore) == 2,
           "restore all should restore exactly the two changed windows");
    const ctm::FixedEntryReport repeated_restore = manager.RestoreAll();
    Expect(repeated_restore.succeeded &&
               controller.Count(ctm::FixedEntryOperationKind::Restore) == 2,
           "repeated restore all should not repeat AddTab");

    const ctm::FixedEntryReport reapplied =
        manager.Synchronize(windows, TestHandle(30));
    Expect(reapplied.succeeded &&
               HandleValue(manager.main_entry()->hwnd) == 20,
           "reapplying after restore all should preserve the fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 4,
           "reapplying should issue one new removal per non-main window");
    static_cast<void>(manager.RestoreAll());
}

void TestNonChromeForegroundUsesStableHandleOrder() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector windows = {
        MakeWindow(50),
        MakeWindow(10),
        MakeWindow(30),
    };

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(999));
    Expect(report.succeeded,
           "a non-Chrome foreground window should not prevent synchronization");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 10,
           "the lowest stable HWND should be selected as the fallback entry");
    static_cast<void>(manager.RestoreAll());
}

void TestFiveWindowsRemoveExactlyFourEntries() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
        MakeWindow(4),
        MakeWindow(5),
    };

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(3));
    Expect(report.succeeded,
           "five windows should synchronize successfully");
    Expect(HandleValue(manager.main_entry()->hwnd) == 3,
           "the foreground window should remain visible with five windows");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 4 &&
               manager.removed_window_count() == 4,
           "five windows should invoke and track four removals");
    static_cast<void>(manager.RestoreAll());
}

void TestNewWindowsAreAddedWithoutChangingTheFixedEntry() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {MakeWindow(1)};
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector expanded = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(expanded, TestHandle(3));
    Expect(report.succeeded,
           "new windows should be incorporated successfully");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 1,
           "new foreground windows should not replace the fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove, 2) == 1 &&
               controller.Count(ctm::FixedEntryOperationKind::Remove, 3) == 1,
           "each new non-main window should receive one removal");
    Expect(manager.removed_window_count() == 2,
           "both new non-main windows should remain tracked");

    static_cast<void>(manager.Synchronize(expanded, TestHandle(2)));
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 2,
           "a repeated lifecycle scan should not remove new windows twice");
    static_cast<void>(manager.RestoreAll());
}

void TestClosedNonMainWindowIsReconciledWithoutExtraRemoval() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector remaining = {
        MakeWindow(1),
        MakeWindow(3),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(remaining, TestHandle(3));
    Expect(report.succeeded,
           "closing a non-main window should reconcile successfully");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 1,
           "closing a non-main window should preserve the fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 2) == 1,
           "the closed non-main identity should be resolved once");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 2,
           "closing a non-main window should not cause extra removals");
    Expect(manager.removed_window_count() == 1,
           "the remaining non-main window should stay tracked");

    static_cast<void>(manager.Synchronize(remaining, TestHandle(3)));
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 2) == 1,
           "a resolved closed identity should not be restored twice");
    static_cast<void>(manager.RestoreAll());
}

void TestAllWindowsCanCloseAndReopenWithNewIdentities() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector<ctm::ChromeWindowSnapshot> empty;
    static_cast<void>(manager.Synchronize(empty, nullptr));

    const std::vector reopened = {
        MakeWindow(40, 400),
        MakeWindow(50, 500),
        MakeWindow(60, 600),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(reopened, TestHandle(50));
    Expect(report.succeeded,
           "new identities should synchronize after every Chrome window closes");
    Expect(manager.main_entry().has_value() &&
               HandleValue(manager.main_entry()->hwnd) == 50,
           "the foreground reopened window should become the new fixed entry");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove, 40) == 1 &&
               controller.Count(ctm::FixedEntryOperationKind::Remove, 60) == 1,
           "reopened non-main windows should each receive one removal");
    Expect(manager.removed_window_count() == 2,
           "only the reopened non-main identities should be tracked");
    static_cast<void>(manager.RestoreAll());
}

void TestOnlySuccessfulRemovalsAreRestored() {
    FakeTaskbarController controller;
    controller.FailNextRemovals(2, 1);
    ctm::FixedEntryManager manager(&controller);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!report.succeeded,
           "a configured DeleteTab failure should fail synchronization");
    Expect(manager.removed_window_count() == 1,
           "only the successful removal should be tracked");

    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded,
           "the one successful removal should restore successfully");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 2) == 0,
           "a failed removal must never receive AddTab during restore all");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 3) == 1,
           "the successful removal should receive exactly one AddTab");
}

void TestClosedMainPromotesAndRestoresTheForegroundWindow() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector remaining = {
        MakeWindow(2),
        MakeWindow(3),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(remaining, TestHandle(3));
    Expect(report.succeeded,
           "closing the main entry should select a replacement safely");
    Expect(HandleValue(manager.main_entry()->hwnd) == 3,
           "the remaining foreground Chrome window should become main");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 3) == 1,
           "a previously removed replacement main should receive AddTab");
    Expect(manager.removed_window_count() == 1,
           "the other remaining window should stay removed");
    static_cast<void>(manager.RestoreAll());
}

void TestReusedHandleRestoresOldIdentityBeforeRemovingNewIdentity() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1, 100),
        MakeWindow(2, 100),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector replaced = {
        MakeWindow(1, 100),
        MakeWindow(2, 999),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(replaced, TestHandle(1));
    Expect(report.succeeded,
           "a reused HWND with a new PID should reconcile safely");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 2) == 1,
           "the old HWND identity should be resolved before reuse");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove, 2) == 2,
           "the new HWND identity should receive its own removal call");
    Expect(manager.removed_window_count() == 1,
           "only the new HWND identity should remain tracked");
    static_cast<void>(manager.RestoreAll());
}

void TestFailedRestorationRemainsTrackedAndRetries() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
    };
    static_cast<void>(manager.Synchronize(windows, TestHandle(1)));
    controller.FailNextRestorations(2, 1);

    const ctm::FixedEntryReport first_restore = manager.RestoreAll();
    Expect(!first_restore.succeeded && manager.removed_window_count() == 1,
           "a failed AddTab should remain tracked for retry");
    const ctm::FixedEntryReport second_restore = manager.RestoreAll();
    Expect(second_restore.succeeded && manager.removed_window_count() == 0,
           "a subsequent AddTab retry should clear the state");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 2) == 2,
           "a failed restoration should be retried exactly once here");
}

void TestReconciliationFailureBlocksNewRemovals() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1),
        MakeWindow(2),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));
    controller.FailNextRestorations(2, 1);

    const std::vector replacement = {
        MakeWindow(3),
        MakeWindow(4),
    };
    const ctm::FixedEntryReport report =
        manager.Synchronize(replacement, TestHandle(3));
    Expect(!report.succeeded,
           "a reconciliation failure should fail the synchronization");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 1,
           "no new DeleteTab calls should occur after restore reconciliation fails");

    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded,
           "restore all should retry the reconciliation failure");
}

void TestTransitionToZeroWindowsRestoresTrackedEntries() {
    FakeTaskbarController controller;
    ctm::FixedEntryManager manager(&controller);
    const std::vector initial = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    static_cast<void>(manager.Synchronize(initial, TestHandle(1)));

    const std::vector<ctm::ChromeWindowSnapshot> empty;
    const ctm::FixedEntryReport report =
        manager.Synchronize(empty, nullptr);
    Expect(report.succeeded,
           "transitioning to zero windows should reconcile successfully");
    Expect(!manager.main_entry().has_value(),
           "transitioning to zero windows should clear the fixed entry");
    Expect(manager.removed_window_count() == 0,
           "transitioning to zero windows should resolve tracked removals");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore) == 2,
           "transitioning to zero windows should restore both changed entries");
}

void TestRecoveryWriteAheadPrecedesEveryRemoval() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    ctm::FixedEntryManager manager(&controller, &store);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(report.succeeded,
           "persistent management should synchronize successfully");
    Expect(store.saved_states.size() == 4,
           "two removals should each write an intent and final state");
    if (store.saved_states.size() == 4) {
        Expect(store.saved_states[0].size() == 1 &&
                   store.saved_states[1].size() == 1 &&
                   store.saved_states[2].size() == 2 &&
                   store.saved_states[3].size() == 2,
               "recovery records should exist before and after each removal");
    }

    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded && manager.removed_window_count() == 0,
           "persistent tracked entries should restore successfully");
    Expect(!store.saved_states.empty() && store.saved_states.back().empty(),
           "successful restoration should persist an empty journal");
}

void TestRecoveryWriteAheadFailureBlocksTaskbarMutation() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    store.fail_on_attempt = 1;
    ctm::FixedEntryManager manager(&controller, &store);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
    };

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!report.succeeded && !report.persistence_error.empty(),
           "a write-ahead failure should fail synchronization clearly");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 0,
           "DeleteTab must not run without a durable recovery intent");
    Expect(manager.removed_window_count() == 0,
           "a blocked removal should leave no in-memory obligation");
}

void TestReadinessGateRunsAfterWriteAheadAndBeforeRemoval() {
    std::vector<std::string> events;
    FakeTaskbarController controller;
    controller.event_log = &events;
    RecordingRecoveryStore store;
    store.event_log = &events;
    RecordingReadinessGate gate;
    gate.event_log = &events;
    ctm::FixedEntryManager manager(&controller, &store, &gate);
    const std::vector windows = {MakeWindow(1), MakeWindow(2)};

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(report.succeeded,
           "a ready internal tab should allow the fake removal");
    const auto save = std::find(events.begin(), events.end(), "save:1");
    const auto confirm =
        std::find(events.begin(), events.end(), "gate:confirm");
    const auto removal =
        std::find(events.begin(), events.end(), "taskbar:remove");
    Expect(save != events.end() && confirm != events.end() &&
               removal != events.end() && save < confirm &&
               confirm < removal,
           "recovery write-ahead must precede readiness confirmation and DeleteTab");

    events.clear();
    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded &&
               std::find(events.begin(), events.end(), "gate:clear") !=
                   events.end(),
           "successful restoration should clear the gate's recovery evidence");
}

void TestReadinessRejectionRollsBackWithoutTaskbarMutation() {
    std::vector<std::string> events;
    FakeTaskbarController controller;
    controller.event_log = &events;
    RecordingRecoveryStore store;
    store.event_log = &events;
    RecordingReadinessGate gate;
    gate.ready = false;
    gate.event_log = &events;
    ctm::FixedEntryManager manager(&controller, &store, &gate);
    const std::vector windows = {MakeWindow(1), MakeWindow(2)};

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!report.succeeded && !report.readiness_error.empty(),
           "a readiness rejection should stop synchronization with a reason");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 0,
           "a readiness rejection must not call the taskbar controller");
    Expect(manager.removed_window_count() == 0 &&
               !store.saved_states.empty() &&
               store.saved_states.back().empty(),
           "a readiness rejection should durably roll back its write-ahead");
    Expect(gate.cleared_handles.size() == 1,
           "a rolled-back intent should be cleared from the readiness model");
}

void TestReadinessRollbackPersistenceFailureRemainsRecoverable() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    store.fail_on_attempt = 2;
    RecordingReadinessGate gate;
    gate.ready = false;
    ctm::FixedEntryManager manager(&controller, &store, &gate);
    const std::vector windows = {MakeWindow(1), MakeWindow(2)};

    const ctm::FixedEntryReport report =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!report.succeeded && !report.persistence_error.empty(),
           "a failed readiness rollback save should report a persistence failure");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 0,
           "rollback persistence failure must still occur before taskbar removal");
    Expect(manager.removed_window_count() == 1,
           "the durable write-ahead should remain tracked until it can be cleared");

    const ctm::FixedEntryReport restored = manager.RestoreAll();
    Expect(restored.succeeded && manager.removed_window_count() == 0 &&
               !store.saved_states.empty() &&
               store.saved_states.back().empty(),
           "a retry should idempotently AddTab and clear the stale write-ahead");
    Expect(gate.cleared_handles.size() == 1,
           "successful retry should clear the model's recovery intent");
}

void TestRealTabGateBlocksMissingTabAndActivationEvidence() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    const std::vector windows = {MakeWindow(1), MakeWindow(2)};
    const std::vector candidates = {
        ctm::TabGroupCandidate{
            .identity = {
                .hwnd = windows[0].hwnd,
                .process_id = windows[0].process_id,
                .thread_id = windows[0].thread_id,
                .process_creation_time = windows[0].process_creation_time,
                .class_name = windows[0].class_name,
            },
            .title = windows[0].title,
        },
        ctm::TabGroupCandidate{
            .identity = {
                .hwnd = windows[1].hwnd,
                .process_id = windows[1].process_id,
                .thread_id = windows[1].thread_id,
                .process_creation_time = windows[1].process_creation_time,
                .class_name = windows[1].class_name,
            },
            .title = windows[1].title,
        },
    };
    ctm::TabGroupModel model;
    static_cast<void>(model.Synchronize(candidates, candidates[0].identity));
    model.SetTabStripHealthy(true);
    ctm::TabGroupTaskbarReadinessGate gate(&model);
    ctm::FixedEntryManager manager(&controller, &store, &gate);

    const ctm::FixedEntryReport missing_tab =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!missing_tab.succeeded &&
               controller.Count(ctm::FixedEntryOperationKind::Remove) == 0,
           "a missing internal tab must block every taskbar removal call");

    Expect(model.MarkTabCreated(candidates[1].identity, true),
           "the second synthetic tab should be creatable");
    const ctm::FixedEntryReport missing_activation =
        manager.Synchronize(windows, TestHandle(1));
    Expect(!missing_activation.succeeded &&
               controller.Count(ctm::FixedEntryOperationKind::Remove) == 0,
           "an unverified activation path must still block taskbar removal");

    Expect(model.MarkActivationPathVerified(candidates[1].identity, true),
           "the second synthetic activation path should become verified");
    const ctm::FixedEntryReport ready =
        manager.Synchronize(windows, TestHandle(1));
    Expect(ready.succeeded &&
               controller.Count(ctm::FixedEntryOperationKind::Remove) == 1,
           "only the fully ready internal tab may reach the taskbar controller");
    static_cast<void>(manager.RestoreAll());
}

void TestTaskbarRecreationForgetsShellStateAndReapplies() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    ctm::FixedEntryManager manager(&controller, &store);
    const std::vector windows = {
        MakeWindow(1),
        MakeWindow(2),
        MakeWindow(3),
    };
    static_cast<void>(manager.Synchronize(windows, TestHandle(1)));
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 2,
           "initial synchronization should remove two entries");

    std::wstring reset_error;
    Expect(manager.ResetAfterTaskbarRecreation(&reset_error),
           "TaskbarCreated should clear obsolete shell state durably");
    Expect(manager.removed_window_count() == 0 &&
               !store.saved_states.empty() &&
               store.saved_states.back().empty(),
           "TaskbarCreated should leave no obsolete recovery entries");

    const ctm::FixedEntryReport reapplied =
        manager.Synchronize(windows, TestHandle(3));
    Expect(reapplied.succeeded,
           "synchronization should reapply after TaskbarCreated");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Remove) == 4,
           "both non-main entries should be removed again after shell rebuild");
    static_cast<void>(manager.RestoreAll());
}

void TestPersistedRecoveryStatesCanBeAdoptedAndRestored() {
    FakeTaskbarController controller;
    RecordingRecoveryStore store;
    ctm::FixedEntryManager manager(&controller, &store);
    ctm::TaskbarMutationState state;
    state.method = ctm::TaskbarMethod::TaskbarList;
    state.identity = {
        .hwnd = TestHandle(9),
        .process_id = 900,
        .thread_id = 901,
        .process_creation_time = 902,
        .class_name = L"Chrome_WidgetWin_1",
    };
    state.modification_applied = true;

    std::wstring adopt_error;
    Expect(manager.AdoptRecoveryStates({state}, &adopt_error),
           "a complete persisted state should be adopted");
    const ctm::FixedEntryReport report = manager.RestoreAll();
    Expect(report.succeeded && manager.removed_window_count() == 0,
           "an adopted state should restore and clear");
    Expect(controller.Count(ctm::FixedEntryOperationKind::Restore, 9) == 1,
           "an adopted state should invoke one exact restoration");
    Expect(!store.saved_states.empty() && store.saved_states.back().empty(),
           "restoring adopted state should clear the persisted journal");
}

}  // namespace

int main() {
    TestZeroAndOneWindowNeedNoTaskbarCalls();
    TestThreeWindowsKeepTheForegroundEntryFixed();
    TestNonChromeForegroundUsesStableHandleOrder();
    TestFiveWindowsRemoveExactlyFourEntries();
    TestNewWindowsAreAddedWithoutChangingTheFixedEntry();
    TestClosedNonMainWindowIsReconciledWithoutExtraRemoval();
    TestAllWindowsCanCloseAndReopenWithNewIdentities();
    TestOnlySuccessfulRemovalsAreRestored();
    TestClosedMainPromotesAndRestoresTheForegroundWindow();
    TestReusedHandleRestoresOldIdentityBeforeRemovingNewIdentity();
    TestFailedRestorationRemainsTrackedAndRetries();
    TestReconciliationFailureBlocksNewRemovals();
    TestTransitionToZeroWindowsRestoresTrackedEntries();
    TestRecoveryWriteAheadPrecedesEveryRemoval();
    TestRecoveryWriteAheadFailureBlocksTaskbarMutation();
    TestReadinessGateRunsAfterWriteAheadAndBeforeRemoval();
    TestReadinessRejectionRollsBackWithoutTaskbarMutation();
    TestReadinessRollbackPersistenceFailureRemainsRecoverable();
    TestRealTabGateBlocksMissingTabAndActivationEvidence();
    TestTaskbarRecreationForgetsShellStateAndReapplies();
    TestPersistedRecoveryStatesCanBeAdoptedAndRestored();

    if (failures != 0) {
        std::cerr << failures << " fixed-entry test(s) failed.\n";
        return 1;
    }
    std::cout << "All fixed-entry tests passed.\n";
    return 0;
}
