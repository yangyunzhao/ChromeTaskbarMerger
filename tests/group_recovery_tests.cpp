#include "group_recovery_journal.h"
#include "restore_command.h"
#include "window_identity_query.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] =
    L"ChromeTaskbarMerger.V2.GroupRecoveryTest";
int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] bool RectanglesEqual(const RECT& left,
                                   const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                (L"ChromeTaskbarMergerGroupRecoveryTests-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        created_ = !error;
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] bool created() const noexcept {
        return created_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    bool created_ = false;
};

LRESULT CALLBACK WindowProcedure(const HWND window,
                                 const UINT message,
                                 const WPARAM wparam,
                                 const LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

class ScopedWindowClass final {
public:
    [[nodiscard]] bool Register() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = WindowProcedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = kWindowClass;
        registered_ = RegisterClassExW(&window_class) != 0;
        return registered_;
    }

    ~ScopedWindowClass() {
        if (registered_) {
            UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
        }
    }

private:
    bool registered_ = false;
};

class ScopedWindow final {
public:
    [[nodiscard]] bool Create(const RECT& rectangle) {
        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kWindowClass,
            L"Group recovery test",
            WS_OVERLAPPEDWINDOW,
            rectangle.left,
            rectangle.top,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (hwnd_ == nullptr) {
            return false;
        }
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        return UpdateWindow(hwnd_) != FALSE;
    }

    ~ScopedWindow() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }

private:
    HWND hwnd_ = nullptr;
};

[[nodiscard]] ctm::WindowIdentity IdentityForWindow(const HWND hwnd) {
    const ctm::WindowIdentityQueryResult query =
        ctm::QueryWindowIdentity(hwnd);
    return query.succeeded ? query.identity : ctm::WindowIdentity{};
}

[[nodiscard]] ctm::GroupMemberRecoveryState MakeMember(
    const std::uintptr_t hwnd,
    const DWORD process_id,
    const std::uint64_t creation_time) {
    ctm::GroupMemberRecoveryState member;
    member.identity = {
        .hwnd = reinterpret_cast<HWND>(hwnd),
        .process_id = process_id,
        .thread_id = process_id + 10,
        .process_creation_time = creation_time,
        .class_name = L"Chrome_WidgetWin_1",
    };
    member.original_placement.length = sizeof(WINDOWPLACEMENT);
    member.original_placement.showCmd = SW_SHOWNORMAL;
    member.original_placement.rcNormalPosition = {
        100, 100, 900, 700};
    member.original_rectangle = {100, 100, 900, 700};
    member.display = {
        .device_name = L"\\\\.\\DISPLAY1",
        .monitor_bounds = {0, 0, 1920, 1080},
        .work_area = {0, 0, 1920, 1040},
    };
    member.tab_created = true;
    member.layout_restore_required = true;
    return member;
}

[[nodiscard]] ctm::TaskbarMutationState MakeTaskbarState(
    const ctm::WindowIdentity& identity) {
    ctm::TaskbarMutationState taskbar;
    taskbar.identity = identity;
    taskbar.method = ctm::TaskbarMethod::TaskbarList;
    taskbar.modification_applied = true;
    return taskbar;
}

[[nodiscard]] ctm::GroupRecoveryState MakeState() {
    ctm::GroupRecoveryState state;
    state.session_active = true;
    state.tab_strip_created = true;
    state.members = {
        MakeMember(0x1234, 100, 1000),
        MakeMember(0x5678, 200, 2000),
    };
    state.taskbar_states = {
        MakeTaskbarState(state.members[1].identity),
    };
    return state;
}

[[nodiscard]] std::string ExtractRecord(
    const std::string& serialized,
    const std::string_view prefix) {
    const std::size_t start = serialized.find(prefix);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = serialized.find('\n', start);
    return serialized.substr(
        start,
        end == std::string::npos ? std::string::npos : end - start + 1);
}

class RecordingPersistence final
    : public ctm::IGroupRecoveryPersistence {
public:
    [[nodiscard]] bool SaveAtomically(
        const std::filesystem::path&,
        const std::string_view serialized,
        std::wstring* const error_message) override {
        ++save_calls;
        if (fail) {
            if (error_message != nullptr) {
                *error_message = L"Injected atomic replacement failure.";
            }
            return false;
        }
        last_serialized.assign(serialized);
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    bool fail = false;
    int save_calls = 0;
    std::string last_serialized;
};

class RecordingGateway final
    : public ctm::IGroupRecoveryWindowGateway {
public:
    [[nodiscard]] ctm::GroupRecoveryTargetCheck Check(
        const ctm::GroupMemberRecoveryState& member) override {
        ++check_calls;
        return member.identity.hwnd == mismatch_handle
                   ? ctm::GroupRecoveryTargetCheck{
                         .status =
                             ctm::GroupRecoveryTargetStatus::IdentityMismatch,
                         .win32_error = ERROR_INVALID_WINDOW_HANDLE,
                         .message = L"Injected stale identity.",
                     }
                   : ctm::GroupRecoveryTargetCheck{
                         .status = ctm::GroupRecoveryTargetStatus::Valid,
                         .message = L"Injected valid target.",
                     };
    }

    [[nodiscard]] ctm::GroupRecoveryTargetCheck Restore(
        const ctm::GroupMemberRecoveryState&) override {
        ++restore_calls;
        return {
            .status = ctm::GroupRecoveryTargetStatus::Valid,
            .message = L"Injected restore succeeded.",
        };
    }

    HWND mismatch_handle = nullptr;
    int check_calls = 0;
    int restore_calls = 0;
};

void TestSerializationRoundTrip() {
    const ctm::GroupRecoveryState state = MakeState();
    const std::string serialized =
        ctm::SerializeGroupRecoveryState(state);
    Expect(!serialized.empty(),
           "a valid group recovery state should serialize");
    const ctm::GroupRecoveryParseResult parsed =
        ctm::ParseGroupRecoveryState(serialized);
    Expect(parsed.succeeded && parsed.state.session_active &&
               parsed.state.tab_strip_created &&
               parsed.state.members.size() == 2 &&
               parsed.state.taskbar_states.size() == 1,
           "the versioned group state should round-trip completely");
    if (parsed.state.members.size() == 2) {
        Expect(parsed.state.members[0].NeedsLayoutRestore() &&
                   parsed.state.members[1].identity.hwnd ==
                       reinterpret_cast<HWND>(0x5678) &&
                   parsed.state.members[1].display.device_name ==
                       L"\\\\.\\DISPLAY1",
               "identity, display, and step state should survive serialization");
    }
}

void TestCorruptTruncatedDuplicateAndOverLimitAreRejected() {
    Expect(!ctm::ParseGroupRecoveryState(
                "ChromeTaskbarMergerGroupRecovery\t2\n"
                "session\t0\t0\n")
                .succeeded,
           "an unknown journal version should be rejected");
    Expect(!ctm::ParseGroupRecoveryState(
                "ChromeTaskbarMergerGroupRecovery\t1\n"
                "session\t1\t0\nmember\t1234\n")
                .succeeded,
           "a truncated member should be rejected");

    ctm::GroupRecoveryState one;
    one.session_active = true;
    one.members = {MakeMember(0x1234, 100, 1000)};
    const std::string serialized =
        ctm::SerializeGroupRecoveryState(one);
    const std::string member = ExtractRecord(serialized, "member\t");
    Expect(!ctm::ParseGroupRecoveryState(serialized + member).succeeded,
           "a duplicate identity should reject the whole journal");

    std::string over_limit =
        "ChromeTaskbarMergerGroupRecovery\t1\nsession\t1\t0\n";
    for (std::size_t index = 0; index < 257; ++index) {
        ctm::GroupRecoveryState item;
        item.session_active = true;
        item.members = {MakeMember(
            0x1000 + index,
            static_cast<DWORD>(100 + index),
            1000 + index)};
        over_limit += ExtractRecord(
            ctm::SerializeGroupRecoveryState(item), "member\t");
    }
    Expect(!ctm::ParseGroupRecoveryState(over_limit).succeeded,
           "more than 256 historical members should be rejected");

    std::string too_large(1024U * 1024U + 1U, 'x');
    Expect(!ctm::ParseGroupRecoveryState(too_large).succeeded,
           "a journal larger than one MiB should be rejected");
}

void TestAtomicPersistenceFailureKeepsPriorState() {
    RecordingPersistence persistence;
    ctm::GroupRecoveryJournal journal(
        L"unused-group-recovery.tsv", &persistence);
    ctm::GroupRecoveryState state = MakeState();
    state.taskbar_states.clear();
    std::wstring error;
    Expect(journal.Adopt(state, &error),
           "a valid in-memory state should be adoptable");

    persistence.fail = true;
    const std::vector taskbar = {
        MakeTaskbarState(state.members[1].identity)};
    Expect(!journal.Save(taskbar, &error) &&
               journal.state().taskbar_states.empty() &&
               error.find(L"Injected") != std::wstring::npos,
           "an atomic replacement failure should not mutate the adopted state");
    Expect(!journal.Clear(&error) && journal.state().session_active,
           "a cleanup write failure should retain every recovery obligation");

    persistence.fail = false;
    Expect(journal.Save(taskbar, &error) &&
               journal.state().taskbar_states.size() == 1 &&
               !persistence.last_serialized.empty(),
           "a later successful atomic save should commit the candidate state");
}

void TestEveryPersistedStepRetainsItsPriorStateOnFailure() {
    RecordingPersistence persistence;
    ctm::GroupRecoveryJournal journal(
        L"unused-group-recovery.tsv", &persistence);
    ctm::GroupRecoveryState state;
    state.session_active = true;
    state.members = {MakeMember(0x1234, 100, 1000)};
    state.members[0].tab_created = false;
    state.members[0].layout_restore_required = false;
    std::wstring error;
    Expect(journal.Adopt(state, &error),
           "the step-failure state should be adoptable");
    const std::array identities = {state.members[0].identity};

    persistence.fail = true;
    Expect(!journal.MarkTabStripCreated(true, &error) &&
               !journal.state().tab_strip_created,
           "a failed tab-strip completion write should keep the prior step");
    persistence.fail = false;
    Expect(journal.MarkTabStripCreated(true, &error),
           "tab-strip completion should be retryable");

    persistence.fail = true;
    Expect(!journal.MarkTabsCreated(identities, true, &error) &&
               !journal.state().members[0].tab_created,
           "a failed tab completion write should keep the prior member step");
    persistence.fail = false;
    Expect(journal.MarkTabsCreated(identities, true, &error),
           "tab completion should be retryable");

    persistence.fail = true;
    Expect(!journal.PlanLayoutMutation(identities, &error) &&
               !journal.state().members[0].layout_restore_required,
           "a failed layout write-ahead should block the layout obligation");
    persistence.fail = false;
    Expect(journal.PlanLayoutMutation(identities, &error),
           "layout write-ahead should be retryable before movement");

    persistence.fail = true;
    Expect(!journal.MarkLayoutRestored(identities[0], &error) &&
               journal.state().members[0].NeedsLayoutRestore(),
           "a failed layout completion write should retain the recovery obligation");
    persistence.fail = false;
    Expect(journal.MarkLayoutRestored(identities[0], &error),
           "layout completion should be retryable");
}

void TestPartialRecoveryAndRepeatedRestoreAreIdempotent() {
    RecordingPersistence persistence;
    ctm::GroupRecoveryJournal journal(
        L"unused-group-recovery.tsv", &persistence);
    ctm::GroupRecoveryState state = MakeState();
    state.taskbar_states.clear();
    std::wstring error;
    Expect(journal.Adopt(state, &error),
           "the partial-recovery state should be adoptable");
    RecordingGateway gateway;
    gateway.mismatch_handle = state.members[1].identity.hwnd;

    const ctm::GroupLayoutRecoveryReport first =
        ctm::RestorePersistedGroupLayouts(&journal, &gateway);
    Expect(first.succeeded && first.restored_count == 1 &&
               first.safely_skipped_count == 1 &&
               gateway.restore_calls == 1,
           "valid layout should restore and a stale identity should be safely skipped");
    const int checks_after_first = gateway.check_calls;
    const ctm::GroupLayoutRecoveryReport repeated =
        ctm::RestorePersistedGroupLayouts(&journal, &gateway);
    Expect(repeated.succeeded && repeated.operations.empty() &&
               gateway.check_calls == checks_after_first &&
               gateway.restore_calls == 1,
           "repeating persisted layout restoration should be a no-op");
}

void RunRealWindowTest() {
    ScopedWindowClass window_class;
    if (!window_class.Register()) {
        Expect(false, "the recovery test window class should register");
        return;
    }
    const RECT original = {180, 160, 980, 760};
    ScopedWindow window;
    if (!window.Create(original)) {
        Expect(false, "the recovery test window should be created");
        return;
    }
    RECT captured_original{};
    GetWindowRect(window.hwnd(), &captured_original);
    const ctm::WindowIdentity identity = IdentityForWindow(window.hwnd());
    Expect(ctm::WindowIdentityIsComplete(identity),
           "the temporary window should expose a complete identity");

    TemporaryDirectory directory;
    if (!directory.created()) {
        Expect(false, "the recovery test directory should be created");
        return;
    }
    const std::filesystem::path path =
        directory.path() / L"recovery-v2.tsv";
    ctm::GroupRecoveryJournal journal(path);
    std::wstring error;
    const std::array identities = {identity};
    Expect(journal.BeginSession(identities, &error) &&
               journal.MarkTabStripCreated(true, &error) &&
               journal.MarkTabsCreated(identities, true, &error) &&
               journal.PlanLayoutMutation(identities, &error),
           "all write-ahead steps should persist before moving a real window");

    ctm::GroupRecoveryJournal loaded(path);
    const ctm::GroupRecoveryLoadResult load = loaded.Load();
    Expect(load.succeeded && load.file_found &&
               load.state.members.size() == 1 &&
               load.state.members[0].NeedsLayoutRestore(),
           "a second process view should load the complete pending session");
    Expect(loaded.Adopt(load.state, &error),
           "the loaded session should become the active recovery state");

    SetWindowPos(
        window.hwnd(),
        nullptr,
        620,
        320,
        640,
        480,
        SWP_NOZORDER | SWP_NOACTIVATE);
    ctm::Win32GroupRecoveryWindowGateway gateway;
    const ctm::GroupLayoutRecoveryReport restored =
        ctm::RestorePersistedGroupLayouts(&loaded, &gateway);
    RECT actual{};
    GetWindowRect(window.hwnd(), &actual);
    Expect(restored.succeeded && restored.restored_count == 1 &&
               RectanglesEqual(actual, captured_original),
           "persisted WINDOWPLACEMENT and exact rectangle should restore a moved real window");

    const ctm::GroupMemberRecoveryState member =
        loaded.state().members.front();
    for (const int mutation : {0, 1, 2, 3}) {
        ctm::GroupMemberRecoveryState stale = member;
        if (mutation == 0) {
            ++stale.identity.process_id;
        } else if (mutation == 1) {
            ++stale.identity.thread_id;
        } else if (mutation == 2) {
            ++stale.identity.process_creation_time;
        } else {
            stale.identity.class_name += L".stale";
        }
        Expect(gateway.Check(stale).status ==
                   ctm::GroupRecoveryTargetStatus::IdentityMismatch,
               "PID, TID, creation time, and class mismatches should each reject recovery");
    }
    ctm::GroupMemberRecoveryState wrong_display = member;
    wrong_display.display.device_name += L".missing";
    Expect(gateway.Check(wrong_display).status ==
               ctm::GroupRecoveryTargetStatus::DisplayMismatch,
           "a missing or changed display identity should reject layout movement");

    Expect(loaded.Clear(&error),
           "the direct layout-recovery state should clear before startup testing");
    ctm::GroupRecoveryJournal startup_journal(path);
    const ctm::GroupRecoveryLoadResult cleared = startup_journal.Load();
    Expect(cleared.succeeded &&
               startup_journal.Adopt(cleared.state, &error) &&
               startup_journal.BeginSession(identities, &error) &&
               startup_journal.PlanLayoutMutation(identities, &error),
           "a fresh crash-like startup recovery state should persist");
    SetWindowPos(
        window.hwnd(),
        nullptr,
        700,
        360,
        600,
        420,
        SWP_NOZORDER | SWP_NOACTIVATE);
    const ctm::StartupGroupRecoveryResult startup =
        ctm::RestorePreviousGroupSession(nullptr, path);
    GetWindowRect(window.hwnd(), &actual);
    Expect(startup.succeeded && startup.recovery_attempted &&
               RectanglesEqual(actual, captured_original),
           "startup should restore an interrupted valid V2 layout before management");
    const ctm::StartupGroupRecoveryResult repeated_startup =
        ctm::RestorePreviousGroupSession(nullptr, path);
    Expect(repeated_startup.succeeded &&
               !repeated_startup.recovery_attempted,
           "repeating startup recovery after cleanup should be a no-op");
}

}  // namespace

int main() {
    TestSerializationRoundTrip();
    TestCorruptTruncatedDuplicateAndOverLimitAreRejected();
    TestAtomicPersistenceFailureKeepsPriorState();
    TestEveryPersistedStepRetainsItsPriorStateOnFailure();
    TestPartialRecoveryAndRepeatedRestoreAreIdempotent();
    RunRealWindowTest();
    if (failures == 0) {
        std::cout << "All V2 group recovery tests passed.\n";
        return 0;
    }
    std::cerr << failures << " V2 group recovery test(s) failed.\n";
    return 1;
}
