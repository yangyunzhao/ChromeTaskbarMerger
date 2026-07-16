#include "chrome_window.h"
#include "command_line.h"
#include "process_identity.h"
#include "taskbar_controller.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }

    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

void TestEmptyArgumentsRunTheApplication() {
    const ctm::CommandLineOptions result = ctm::ParseCommandLine({});
    Expect(result.command == ctm::Command::Run,
           "empty arguments should select the run command");
    Expect(result.error_message.empty(),
           "empty arguments should not produce an error");
}

void TestHelpAliases() {
    constexpr std::array<std::wstring_view, 3> aliases = {
        L"--help",
        L"-h",
        L"/?",
    };

    for (const std::wstring_view alias : aliases) {
        const std::array arguments = {alias};
        const ctm::CommandLineOptions result =
            ctm::ParseCommandLine(arguments);
        Expect(result.command == ctm::Command::ShowHelp,
               "every help alias should select the help command");
        Expect(result.error_message.empty(),
               "help aliases should not produce an error");
    }
}

void TestVersionOption() {
    constexpr std::array arguments = {std::wstring_view(L"--version")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::ShowVersion,
           "--version should select the version command");
    Expect(result.error_message.empty(),
           "--version should not produce an error");
}

void TestAutoStartOption() {
    constexpr std::array arguments = {std::wstring_view(L"--autostart")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::AutoStart,
           "--autostart should select the Windows-login launch command");
    Expect(result.error_message.empty(),
           "--autostart should not produce an error");
}

void TestListOption() {
    constexpr std::array arguments = {std::wstring_view(L"--list")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::ListWindows,
           "--list should select the Chrome window listing command");
    Expect(result.error_message.empty(),
           "--list should not produce an error");
}

void TestExperimentOption() {
    constexpr std::array arguments = {std::wstring_view(L"--experiment")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::Experiment,
           "--experiment should select the Phase 2 experiment command");
    Expect(result.error_message.empty(),
           "--experiment should not produce an error");
}

void TestV2ExperimentOption() {
    constexpr std::array arguments = {
        std::wstring_view(L"--v2-experiment")};
    const ctm::CommandLineOptions result =
        ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::V2Experiment,
           "--v2-experiment should select the isolated V2 lifecycle command");
    Expect(result.error_message.empty(),
           "--v2-experiment should not produce an error");
}

void TestManageOption() {
    constexpr std::array arguments = {std::wstring_view(L"--manage")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::Manage,
           "--manage should select the lifecycle-management command");
    Expect(result.error_message.empty(),
           "--manage should not produce an error");
}

void TestRestoreAllOption() {
    constexpr std::array arguments = {std::wstring_view(L"--restore-all")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::RestoreAll,
           "--restore-all should select the explicit recovery command");
    Expect(result.error_message.empty(),
           "--restore-all should not produce an error");
}

void TestUnknownOption() {
    constexpr std::array arguments = {std::wstring_view(L"--unknown")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::Invalid,
           "an unknown option should be rejected");
    Expect(!result.error_message.empty(),
           "an unknown option should explain the error");
}

void TestMultipleOptions() {
    constexpr std::array arguments = {
        std::wstring_view(L"--help"),
        std::wstring_view(L"--version"),
    };
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::Invalid,
           "multiple options should be rejected in Phase 0");
    Expect(!result.error_message.empty(),
           "multiple options should explain the error");
}

[[nodiscard]] ctm::ChromeWindowSnapshot MakeManageableChromeSnapshot() {
    ctm::ChromeWindowSnapshot snapshot;
    snapshot.process_id = 42;
    snapshot.thread_id = 84;
    snapshot.process_creation_time = 123456789;
    snapshot.process_path =
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    snapshot.title = L"Example - Google Chrome";
    snapshot.class_name = L"Chrome_WidgetWin_1";
    snapshot.style = WS_OVERLAPPEDWINDOW;
    snapshot.extended_style = 0;
    snapshot.process_path_available = true;
    snapshot.process_creation_time_available = true;
    snapshot.class_name_available = true;
    snapshot.style_available = true;
    snapshot.extended_style_available = true;
    snapshot.top_level = true;
    snapshot.visible = true;
    snapshot.cloaked_query_available = true;
    return snapshot;
}

[[nodiscard]] bool HasReason(
    const ctm::ChromeWindowAssessment& assessment,
    const ctm::WindowExclusionReason reason) {
    return std::find(
               assessment.exclusion_reasons.begin(),
               assessment.exclusion_reasons.end(),
               reason) != assessment.exclusion_reasons.end();
}

void TestManageableChromeWindow() {
    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(MakeManageableChromeSnapshot());
    Expect(assessment.chrome_process,
           "a canonical Chrome snapshot should be identified as Chrome");
    Expect(assessment.manageable,
           "a canonical Chrome snapshot should be manageable");
    Expect(assessment.exclusion_reasons.empty(),
           "a manageable Chrome snapshot should have no exclusion reasons");
}

void TestChromeExecutableComparisonIsCaseInsensitive() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.process_path = L"C:\\Chrome\\CHROME.EXE";
    snapshot.class_name = L"Chrome_WidgetWin_42";

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(assessment.manageable,
           "Chrome executable matching should be case-insensitive and the "
           "class suffix should remain flexible");
}

void TestNonChromeProcessIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.process_path = L"C:\\Windows\\notepad.exe";

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(!assessment.chrome_process,
           "a non-Chrome executable should not be a Chrome candidate");
    Expect(!assessment.manageable,
           "a non-Chrome executable should not be manageable");
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::NotChromeExecutable),
           "a non-Chrome executable should report its exclusion reason");
}

void TestUnavailableProcessPathIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.process_path.clear();
    snapshot.process_path_available = false;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::ProcessPathUnavailable),
           "an unavailable process path should be reported");
}

void TestInvisibleWindowIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.visible = false;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(!assessment.manageable,
           "an invisible Chrome window should not be manageable");
    Expect(HasReason(assessment, ctm::WindowExclusionReason::NotVisible),
           "an invisible Chrome window should report its exclusion reason");
}

void TestEmptyTitleIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.title.clear();

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(assessment, ctm::WindowExclusionReason::EmptyTitle),
           "an empty title should be reported");
}

void TestToolWindowIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.extended_style |= WS_EX_TOOLWINDOW;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(assessment, ctm::WindowExclusionReason::ToolWindow),
           "a tool window should be reported");
}

void TestChildWindowIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.style |= WS_CHILD;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(assessment, ctm::WindowExclusionReason::ChildWindow),
           "a child window should be reported");
}

void TestNoActivateWindowIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.extended_style |= WS_EX_NOACTIVATE;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(assessment, ctm::WindowExclusionReason::NoActivate),
           "a no-activate window should be reported");
}

void TestUnsupportedChromeClassIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.class_name = L"Chrome_RenderWidgetHostHWND";

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::UnsupportedWindowClass),
           "an unsupported Chrome class should be reported");
}

void TestUnavailableWindowMetadataIsRejected() {
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.class_name.clear();
    snapshot.class_name_available = false;
    snapshot.style_available = false;
    snapshot.extended_style_available = false;

    const ctm::ChromeWindowAssessment assessment =
        ctm::EvaluateChromeWindow(snapshot);
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::ClassNameUnavailable),
           "an unavailable class name should be reported");
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::StyleUnavailable),
           "an unavailable style should be reported");
    Expect(HasReason(
               assessment,
               ctm::WindowExclusionReason::ExtendedStyleUnavailable),
           "an unavailable extended style should be reported");
}

void TestEmptyEnumerationSummary() {
    ctm::ChromeWindowEnumerationResult result;
    result.succeeded = true;
    const std::wstring summary = ctm::FormatChromeWindowSummary(result);
    Expect(summary.find(L"Chrome candidates: 0") != std::wstring::npos,
           "an empty enumeration should report zero Chrome candidates");
    Expect(summary.find(L"Manageable: 0; Excluded: 0") !=
               std::wstring::npos,
           "an empty enumeration should report zero classified windows");
}

void TestTaskbarHiddenStyleCalculation() {
    constexpr LONG_PTR original =
        static_cast<LONG_PTR>(WS_EX_APPWINDOW | WS_EX_TOPMOST |
                              WS_EX_WINDOWEDGE);
    const LONG_PTR hidden =
        ctm::CalculateTaskbarHiddenExtendedStyle(original);
    Expect((hidden & WS_EX_APPWINDOW) == 0,
           "the hidden style should clear WS_EX_APPWINDOW");
    Expect((hidden & WS_EX_TOOLWINDOW) != 0,
           "the hidden style should set WS_EX_TOOLWINDOW");
    Expect((hidden & WS_EX_TOPMOST) != 0 && (hidden & WS_EX_WINDOWEDGE) != 0,
           "unrelated extended-style bits should be preserved");
}

void TestTaskbarMutationStateTransitions() {
    ctm::TaskbarMutationState state;
    Expect(!state.NeedsRestore(),
           "a fresh mutation state should not need restoration");
    state.modification_applied = true;
    Expect(state.NeedsRestore(),
           "an applied modification should need restoration");
    state.restore_completed = true;
    Expect(!state.NeedsRestore(),
           "a completed restoration should be idempotent");
}

void TestWindowIdentityMatching() {
    ctm::WindowIdentity identity;
    identity.process_id = 10;
    identity.thread_id = 20;
    identity.process_creation_time = 30;
    identity.class_name = L"Chrome_WidgetWin_1";
    Expect(ctm::WindowIdentityValuesMatch(
               identity, 10, 20, 30, L"Chrome_WidgetWin_1"),
           "matching PID, TID, and class should preserve window identity");
    Expect(!ctm::WindowIdentityValuesMatch(
               identity, 11, 20, 30, L"Chrome_WidgetWin_1"),
           "a changed PID should reject a possibly reused HWND");
    Expect(!ctm::WindowIdentityValuesMatch(
               identity, 10, 20, 30, L"OtherClass"),
           "a changed class should reject a possibly reused HWND");
    Expect(!ctm::WindowIdentityValuesMatch(
               identity, 10, 20, 31, L"Chrome_WidgetWin_1"),
           "a changed process creation time should reject a reused PID");
}

void TestRestoreWithoutModificationIsSafeAndIdempotent() {
    ctm::TaskbarController controller;
    ctm::TaskbarMutationState state;
    const ctm::TaskbarOperationResult result =
        controller.RestoreWindow(&state);
    Expect(result.succeeded,
           "restoring an untouched state should succeed safely");
    Expect(result.skipped,
           "restoring an untouched state should skip all window APIs");
    Expect(!state.NeedsRestore(),
           "restoring an untouched state should remain idempotent");
}

void TestInvalidWindowRemovalIsSafe() {
    ctm::TaskbarController controller;
    ctm::TaskbarMutationState state;
    ctm::ChromeWindowSnapshot snapshot = MakeManageableChromeSnapshot();
    snapshot.hwnd = nullptr;
    const ctm::TaskbarOperationResult result = controller.RemoveWindow(
        snapshot, ctm::TaskbarMethod::WindowStyle, &state);
    Expect(!result.succeeded,
           "removing an invalid HWND should fail without a mutation");
    Expect(!state.NeedsRestore(),
           "an invalid HWND should not leave a restoration obligation");
}

struct ScopedTestWindow {
    HWND hwnd = nullptr;

    ~ScopedTestWindow() {
        if (hwnd != nullptr) {
            DestroyWindow(hwnd);
        }
    }

    ScopedTestWindow() = default;
    ScopedTestWindow(const ScopedTestWindow&) = delete;
    ScopedTestWindow& operator=(const ScopedTestWindow&) = delete;
};

[[nodiscard]] ctm::ChromeWindowSnapshot MakeSnapshotForTestWindow(
    const HWND hwnd) {
    ctm::ChromeWindowSnapshot snapshot;
    snapshot.hwnd = hwnd;
    snapshot.thread_id =
        GetWindowThreadProcessId(hwnd, &snapshot.process_id);
    const ctm::ProcessCreationTimeResult creation_time =
        ctm::QueryProcessCreationTime(snapshot.process_id);
    snapshot.process_creation_time = creation_time.value;

    std::array<wchar_t, 256> class_name{};
    const int copied = GetClassNameW(
        hwnd, class_name.data(), static_cast<int>(class_name.size()));
    if (copied > 0) {
        snapshot.class_name.assign(
            class_name.data(), static_cast<std::size_t>(copied));
    }
    return snapshot;
}

[[nodiscard]] LONG_PTR ReadExtendedStyleForTest(const HWND hwnd) {
    SetLastError(ERROR_SUCCESS);
    return GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
}

void TestWindowStyleRoundTripUsesOnlyTheSelectedWindow() {
    constexpr DWORD initial_extended_style =
        WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_WINDOWEDGE;
    ScopedTestWindow selected;
    ScopedTestWindow untouched;
    selected.hwnd = CreateWindowExW(
        initial_extended_style,
        L"STATIC",
        L"ChromeTaskbarMerger selected test window",
        WS_OVERLAPPED,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    untouched.hwnd = CreateWindowExW(
        initial_extended_style,
        L"STATIC",
        L"ChromeTaskbarMerger untouched test window",
        WS_OVERLAPPED,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    Expect(selected.hwnd != nullptr && untouched.hwnd != nullptr,
           "hidden synthetic test windows should be created");
    if (selected.hwnd == nullptr || untouched.hwnd == nullptr) {
        return;
    }

    const LONG_PTR selected_before =
        ReadExtendedStyleForTest(selected.hwnd);
    const LONG_PTR untouched_before =
        ReadExtendedStyleForTest(untouched.hwnd);

    ctm::TaskbarController controller;
    ctm::TaskbarMutationState state;
    const ctm::TaskbarOperationResult removal = controller.RemoveWindow(
        MakeSnapshotForTestWindow(selected.hwnd),
        ctm::TaskbarMethod::WindowStyle,
        &state);
    Expect(removal.succeeded,
           "the selected synthetic window style should be changed");
    Expect(state.NeedsRestore(),
           "a successful synthetic style change should require restoration");
    Expect(ReadExtendedStyleForTest(selected.hwnd) ==
               ctm::CalculateTaskbarHiddenExtendedStyle(selected_before),
           "the selected synthetic window should receive the hidden style");
    Expect(ReadExtendedStyleForTest(untouched.hwnd) == untouched_before,
           "an unselected synthetic window should remain untouched");

    const ctm::TaskbarOperationResult restoration =
        controller.RestoreWindow(&state);
    Expect(restoration.succeeded,
           "the selected synthetic window style should be restored");
    Expect(ReadExtendedStyleForTest(selected.hwnd) == selected_before,
           "restoration should reproduce the exact original extended style");
    Expect(ReadExtendedStyleForTest(untouched.hwnd) == untouched_before,
           "restoration should still leave the unselected window untouched");

    const ctm::TaskbarOperationResult repeated_restoration =
        controller.RestoreWindow(&state);
    Expect(repeated_restoration.succeeded && repeated_restoration.skipped,
           "repeated restoration should be safe and idempotent");
}

void TestRestoreSkipsAChangedWindowIdentity() {
    ScopedTestWindow window;
    window.hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"STATIC",
        L"ChromeTaskbarMerger reused HWND test window",
        WS_OVERLAPPED,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    Expect(window.hwnd != nullptr,
           "a synthetic identity test window should be created");
    if (window.hwnd == nullptr) {
        return;
    }

    const LONG_PTR style_before = ReadExtendedStyleForTest(window.hwnd);
    ctm::TaskbarMutationState state;
    state.method = ctm::TaskbarMethod::WindowStyle;
    state.identity.hwnd = window.hwnd;
    state.identity.process_id = GetCurrentProcessId() + 1;
    state.identity.thread_id = GetCurrentThreadId();
    state.identity.process_creation_time =
        ctm::QueryProcessCreationTime(GetCurrentProcessId()).value;
    state.identity.class_name = L"STATIC";
    state.original_extended_style = 0;
    state.modification_applied = true;

    ctm::TaskbarController controller;
    const ctm::TaskbarOperationResult restoration =
        controller.RestoreWindow(&state);
    Expect(restoration.succeeded && restoration.skipped,
           "restoration should safely skip a changed window identity");
    Expect(!state.NeedsRestore(),
           "a rejected reused HWND should resolve the stale restore state");
    Expect(ReadExtendedStyleForTest(window.hwnd) == style_before,
           "a changed window identity should never receive a saved style");
}

void TestRestoreSkipsAChangedProcessCreationTime() {
    ScopedTestWindow window;
    window.hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"STATIC",
        L"ChromeTaskbarMerger process creation identity test",
        WS_OVERLAPPED,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    Expect(window.hwnd != nullptr,
           "a synthetic process-creation identity window should be created");
    if (window.hwnd == nullptr) {
        return;
    }

    ctm::TaskbarMutationState state;
    state.method = ctm::TaskbarMethod::WindowStyle;
    state.identity.hwnd = window.hwnd;
    state.identity.thread_id = GetWindowThreadProcessId(
        window.hwnd, &state.identity.process_id);
    state.identity.process_creation_time =
        ctm::QueryProcessCreationTime(state.identity.process_id).value + 1;
    state.identity.class_name = L"STATIC";
    state.original_extended_style = 0;
    state.modification_applied = true;

    const LONG_PTR style_before = ReadExtendedStyleForTest(window.hwnd);
    ctm::TaskbarController controller;
    const ctm::TaskbarOperationResult restoration =
        controller.RestoreWindow(&state);
    Expect(restoration.succeeded && restoration.skipped,
           "a changed process creation time should skip stale recovery");
    Expect(!state.NeedsRestore(),
           "a stale process creation identity should be resolved safely");
    Expect(ReadExtendedStyleForTest(window.hwnd) == style_before,
           "stale process creation state must not modify the current window");
}

void TestTaskbarListCanInitializeWithoutMutation() {
    ctm::TaskbarController controller;
    const ctm::TaskbarOperationResult result =
        controller.InitializeTaskbarList();
    Expect(result.succeeded,
           "ITaskbarList COM initialization and HrInit should succeed");
    controller.Shutdown();
}

}  // namespace

int main() {
    TestEmptyArgumentsRunTheApplication();
    TestHelpAliases();
    TestVersionOption();
    TestAutoStartOption();
    TestListOption();
    TestExperimentOption();
    TestV2ExperimentOption();
    TestManageOption();
    TestRestoreAllOption();
    TestUnknownOption();
    TestMultipleOptions();
    TestManageableChromeWindow();
    TestChromeExecutableComparisonIsCaseInsensitive();
    TestNonChromeProcessIsRejected();
    TestUnavailableProcessPathIsRejected();
    TestInvisibleWindowIsRejected();
    TestEmptyTitleIsRejected();
    TestToolWindowIsRejected();
    TestChildWindowIsRejected();
    TestNoActivateWindowIsRejected();
    TestUnsupportedChromeClassIsRejected();
    TestUnavailableWindowMetadataIsRejected();
    TestEmptyEnumerationSummary();
    TestTaskbarHiddenStyleCalculation();
    TestTaskbarMutationStateTransitions();
    TestWindowIdentityMatching();
    TestRestoreWithoutModificationIsSafeAndIdempotent();
    TestInvalidWindowRemovalIsSafe();
    TestWindowStyleRoundTripUsesOnlyTheSelectedWindow();
    TestRestoreSkipsAChangedWindowIdentity();
    TestRestoreSkipsAChangedProcessCreationTime();
    TestTaskbarListCanInitializeWithoutMutation();

    if (failures != 0) {
        std::cerr << failures << " core test(s) failed.\n";
        return 1;
    }

    std::cout << "All core tests passed.\n";
    return 0;
}
