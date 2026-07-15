#include "fixed_entry_manager.h"

#include <Windows.h>

#include <cstdint>
#include <iostream>
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
    snapshot.class_name = L"Chrome_WidgetWin_1";
    snapshot.title = L"Synthetic Chrome window";
    return snapshot;
}

struct FakeCall {
    ctm::FixedEntryOperationKind kind =
        ctm::FixedEntryOperationKind::Remove;
    ctm::WindowIdentity identity;
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
            .class_name = snapshot.class_name,
        };
        calls.push_back({
            .kind = ctm::FixedEntryOperationKind::Remove,
            .identity = identity,
        });

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

    if (failures != 0) {
        std::cerr << failures << " fixed-entry test(s) failed.\n";
        return 1;
    }
    std::cout << "All fixed-entry tests passed.\n";
    return 0;
}
