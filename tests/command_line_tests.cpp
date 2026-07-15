#include "chrome_window.h"
#include "command_line.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
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

void TestListOption() {
    constexpr std::array arguments = {std::wstring_view(L"--list")};
    const ctm::CommandLineOptions result = ctm::ParseCommandLine(arguments);
    Expect(result.command == ctm::Command::ListWindows,
           "--list should select the Chrome window listing command");
    Expect(result.error_message.empty(),
           "--list should not produce an error");
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
    snapshot.process_path =
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    snapshot.title = L"Example - Google Chrome";
    snapshot.class_name = L"Chrome_WidgetWin_1";
    snapshot.style = WS_OVERLAPPEDWINDOW;
    snapshot.extended_style = 0;
    snapshot.process_path_available = true;
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

}  // namespace

int main() {
    TestEmptyArgumentsRunTheApplication();
    TestHelpAliases();
    TestVersionOption();
    TestListOption();
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

    if (failures != 0) {
        std::cerr << failures << " core test(s) failed.\n";
        return 1;
    }

    std::cout << "All core tests passed.\n";
    return 0;
}
