#include "taskbar_experiment.h"

#include "chrome_window.h"
#include "logger.h"
#include "taskbar_controller.h"

#include <Windows.h>

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {
namespace {

enum class Observation {
    Yes,
    No,
    Unknown,
};

class ConsoleCtrlCGuard final {
public:
    [[nodiscard]] bool Activate() {
        if (active_) {
            return true;
        }
        active_ = SetConsoleCtrlHandler(nullptr, TRUE) != FALSE;
        return active_;
    }

    ~ConsoleCtrlCGuard() {
        if (active_) {
            SetConsoleCtrlHandler(nullptr, FALSE);
        }
    }

    ConsoleCtrlCGuard(const ConsoleCtrlCGuard&) = delete;
    ConsoleCtrlCGuard& operator=(const ConsoleCtrlCGuard&) = delete;

    ConsoleCtrlCGuard() = default;

private:
    bool active_ = false;
};

class EmergencyRestoreGuard final {
public:
    EmergencyRestoreGuard(TaskbarController* const controller,
                          TaskbarMutationState* const state,
                          Logger* const logger) noexcept
        : controller_(controller), state_(state), logger_(logger) {}

    ~EmergencyRestoreGuard() noexcept {
        if (controller_ == nullptr || state_ == nullptr ||
            !state_->NeedsRestore()) {
            return;
        }

        try {
            const TaskbarOperationResult result =
                controller_->RestoreWindow(state_);
            if (logger_ != nullptr) {
                if (result.succeeded) {
                    logger_->Info(
                        L"Emergency scope-exit restoration completed.");
                } else {
                    logger_->Error(
                        L"Emergency scope-exit restoration failed: " +
                        result.message);
                }
            }
        } catch (...) {
            // Destructors cannot guarantee recovery after catastrophic failures.
        }
    }

    EmergencyRestoreGuard(const EmergencyRestoreGuard&) = delete;
    EmergencyRestoreGuard& operator=(const EmergencyRestoreGuard&) = delete;

private:
    TaskbarController* controller_ = nullptr;
    TaskbarMutationState* state_ = nullptr;
    Logger* logger_ = nullptr;
};

[[nodiscard]] std::wstring FormatHex(const std::uint64_t value,
                                     const int width) {
    std::wostringstream output;
    output << L"0x" << std::uppercase << std::hex << std::setfill(L'0')
           << std::setw(width) << value;
    return output.str();
}

[[nodiscard]] std::wstring FormatOperationResult(
    const std::wstring_view operation,
    const TaskbarMethod method,
    const TaskbarOperationResult& result) {
    std::wostringstream output;
    output << operation << L": " << (result.succeeded ? L"SUCCESS" : L"FAIL")
           << L'\n'
           << L"  Method: " << TaskbarMethodText(method) << L'\n'
           << L"  HRESULT: "
           << FormatHex(
                  static_cast<std::uint32_t>(result.hresult),
                  8)
           << L'\n'
           << L"  Win32 error: " << result.win32_error << L" ("
           << FormatHex(result.win32_error, 8) << L")\n"
           << L"  ExStyle before: "
           << FormatHex(
                  static_cast<std::uint64_t>(
                      static_cast<std::uintptr_t>(
                          result.extended_style_before)),
                  static_cast<int>(sizeof(LONG_PTR) * 2))
           << L'\n'
           << L"  ExStyle after:  "
           << FormatHex(
                  static_cast<std::uint64_t>(
                      static_cast<std::uintptr_t>(
                          result.extended_style_after)),
                  static_cast<int>(sizeof(LONG_PTR) * 2))
           << L'\n'
           << L"  State changed: " << (result.state_changed ? L"yes" : L"no")
           << L"; Skipped: " << (result.skipped ? L"yes" : L"no")
           << L'\n'
           << L"  Message: " << result.message;
    return output.str();
}

void WriteReport(Logger* const logger, const std::wstring& report) {
    std::wcout << report << L'\n';
    if (logger != nullptr) {
        logger->Info(report);
    }
}

[[nodiscard]] std::optional<std::size_t> ReadChoice(
    const std::wstring_view prompt,
    const std::size_t maximum) {
    while (true) {
        std::wcout << prompt << std::flush;
        std::wstring input;
        if (!std::getline(std::wcin, input)) {
            return std::nullopt;
        }

        std::wistringstream parser(input);
        std::size_t value = 0;
        wchar_t trailing = L'\0';
        if ((parser >> value) && !(parser >> trailing) && value <= maximum) {
            return value;
        }
        std::wcout << L"Please enter a number from 0 to " << maximum
                   << L".\n";
    }
}

[[nodiscard]] Observation ReadObservation(
    const std::wstring_view prompt) {
    while (true) {
        std::wcout << prompt << L" [y/n/u]: " << std::flush;
        std::wstring input;
        if (!std::getline(std::wcin, input)) {
            return Observation::Unknown;
        }
        if (input == L"y" || input == L"Y") {
            return Observation::Yes;
        }
        if (input == L"n" || input == L"N") {
            return Observation::No;
        }
        if (input == L"u" || input == L"U" || input.empty()) {
            return Observation::Unknown;
        }
        std::wcout << L"Enter y, n, or u.\n";
    }
}

[[nodiscard]] std::wstring_view ObservationText(
    const Observation observation) noexcept {
    switch (observation) {
        case Observation::Yes:
            return L"yes";
        case Observation::No:
            return L"no";
        case Observation::Unknown:
            return L"unknown";
    }
    return L"unknown";
}

void LogObservation(Logger* const logger,
                    const std::wstring_view name,
                    const Observation observation) {
    if (logger == nullptr) {
        return;
    }
    logger->Info(
        std::wstring(name) + L": " + std::wstring(ObservationText(observation)));
}

[[nodiscard]] int RunTaskbarExperimentImpl(Logger* const logger) {
    std::wcout
        << L"=== ChromeTaskbarMerger Phase 2 experiment ===\n"
        << L"This command can temporarily change one Chrome taskbar entry.\n"
        << L"It will not hide or close the Chrome window.\n"
        << L"Do not terminate this process after applying a change; wait for "
           L"the restore step.\n\n";

    const ChromeWindowEnumerationResult enumeration =
        EnumerateChromeWindows();
    if (!enumeration.succeeded) {
        const std::wstring message =
            L"Window enumeration failed with Win32 error " +
            std::to_wstring(enumeration.error_code) + L'.';
        std::wcerr << message << L'\n';
        if (logger != nullptr) {
            logger->Error(message);
        }
        return 3;
    }

    std::vector<const ChromeWindowRecord*> manageable_windows;
    for (const ChromeWindowRecord& record : enumeration.chrome_windows) {
        if (record.assessment.manageable) {
            manageable_windows.push_back(&record);
        }
    }

    if (manageable_windows.size() < 2) {
        std::wcout << L"At least two manageable Chrome windows are required; "
                      L"found "
                   << manageable_windows.size() << L".\n";
        return 4;
    }

    std::wcout << L"Manageable Chrome windows:\n";
    for (std::size_t index = 0; index < manageable_windows.size(); ++index) {
        std::wcout << L"\n"
                   << FormatChromeWindowRecord(
                          *manageable_windows[index], index + 1)
                   << L'\n';
    }

    const std::optional<std::size_t> selected_index = ReadChoice(
        L"Select a window number, or 0 to cancel: ",
        manageable_windows.size());
    if (!selected_index.has_value() || *selected_index == 0) {
        std::wcout << L"Experiment cancelled before any modification.\n";
        return 0;
    }

    std::wcout
        << L"\nMethods:\n"
        << L"  1. ITaskbarList::DeleteTab / AddTab\n"
        << L"  2. WS_EX_APPWINDOW / WS_EX_TOOLWINDOW\n";
    const std::optional<std::size_t> method_choice =
        ReadChoice(L"Select a method, or 0 to cancel: ", 2);
    if (!method_choice.has_value() || *method_choice == 0) {
        std::wcout << L"Experiment cancelled before any modification.\n";
        return 0;
    }

    const TaskbarMethod method =
        *method_choice == 1 ? TaskbarMethod::TaskbarList
                            : TaskbarMethod::WindowStyle;
    TaskbarController controller;
    if (method == TaskbarMethod::TaskbarList) {
        const TaskbarOperationResult initialization =
            controller.InitializeTaskbarList();
        const std::wstring report = FormatOperationResult(
            L"Initialize ITaskbarList", method, initialization);
        WriteReport(logger, report);
        if (!initialization.succeeded) {
            return 4;
        }
    }

    const ChromeWindowRecord& selected =
        *manageable_windows[*selected_index - 1];
    std::wcout << L"\nSelected window:\n"
               << FormatChromeWindowRecord(selected, *selected_index)
               << L"\nMethod: " << TaskbarMethodText(method) << L'\n';
    if (method == TaskbarMethod::WindowStyle) {
        std::wcout << L"Proposed ExStyle: "
                   << FormatHex(
                          static_cast<std::uint64_t>(
                              static_cast<std::uintptr_t>(
                                  CalculateTaskbarHiddenExtendedStyle(
                                      selected.snapshot.extended_style))),
                          static_cast<int>(sizeof(LONG_PTR) * 2))
                   << L'\n';
    }

    std::wcout << L"Type APPLY to perform the temporary change: "
               << std::flush;
    std::wstring confirmation;
    if (!std::getline(std::wcin, confirmation) || confirmation != L"APPLY") {
        std::wcout << L"Experiment cancelled before any modification.\n";
        return 0;
    }

    if (!IsManageableChromeWindow(selected.snapshot.hwnd)) {
        std::wcerr << L"The selected window changed and is no longer a "
                      L"manageable Chrome main window.\n";
        return 4;
    }

    ConsoleCtrlCGuard ctrl_c_guard;
    if (!ctrl_c_guard.Activate()) {
        const DWORD error_code = GetLastError();
        std::wcerr << L"Unable to install the temporary Ctrl+C safety guard "
                      L"(Win32 error "
                   << error_code << L"). No modification was made.\n";
        return 4;
    }

    TaskbarMutationState state;
    EmergencyRestoreGuard emergency_restore(&controller, &state, logger);
    const TaskbarOperationResult removal = controller.RemoveWindow(
        selected.snapshot, method, &state);
    WriteReport(
        logger,
        FormatOperationResult(L"Temporary removal", method, removal));

    Observation removal_observation = Observation::Unknown;
    Observation usable_observation = Observation::Unknown;
    Observation window_tabs_observation = Observation::Unknown;
    Observation alt_tab_observation = Observation::Unknown;
    if (removal.succeeded) {
        std::wcout
            << L"\nObserve the taskbar and target Chrome window now. WindowTabs "
               L"and Alt+Tab are compatibility observations only; either may "
               L"be affected in V1.\n";
        removal_observation = ReadObservation(
            L"Did the selected taskbar button disappear");
        usable_observation = ReadObservation(
            L"Is the selected Chrome window still open, reachable, and usable");
        window_tabs_observation = ReadObservation(
            L"Can WindowTabs still reach/switch the selected window");
        alt_tab_observation = ReadObservation(
            L"Can Alt+Tab still reach the selected window");
    }

    LogObservation(logger, L"Button disappeared", removal_observation);
    LogObservation(logger, L"Chrome window remained usable", usable_observation);
    LogObservation(logger, L"WindowTabs remained usable", window_tabs_observation);
    LogObservation(logger, L"Alt+Tab remained usable", alt_tab_observation);

    if (!state.NeedsRestore()) {
        std::wcout << L"No modification was applied, so no restore is needed.\n";
        return removal.succeeded ? 0 : 4;
    }

    std::wcout << L"\nRestoring the selected window now...\n";
    TaskbarOperationResult restoration = controller.RestoreWindow(&state);
    if (!restoration.succeeded && state.NeedsRestore()) {
        std::wcerr << L"First restoration attempt failed; retrying once.\n";
        restoration = controller.RestoreWindow(&state);
    }
    WriteReport(
        logger,
        FormatOperationResult(L"Restoration", method, restoration));

    if (!restoration.succeeded || state.NeedsRestore()) {
        std::wcerr
            << L"WARNING: automatic restoration did not complete. Keep this "
               L"log, retry the experiment only after inspecting the window, "
               L"and restart Explorer if the taskbar entry remains missing.\n";
        return 5;
    }

    const Observation restoration_observation = ReadObservation(
        L"Did the taskbar button reappear without restarting Explorer");
    LogObservation(logger, L"Button restored", restoration_observation);

    const bool visual_failure =
        removal_observation == Observation::No ||
        usable_observation == Observation::No ||
        restoration_observation == Observation::No;
    const bool visual_incomplete =
        removal_observation == Observation::Unknown ||
        usable_observation == Observation::Unknown ||
        restoration_observation == Observation::Unknown;
    const std::wstring_view visual_verdict =
        !removal.succeeded
            ? L"API FAILURE"
            : (visual_failure ? L"FAIL"
                              : (visual_incomplete ? L"INCOMPLETE" : L"PASS"));

    std::wcout << L"\nExperiment observation summary:\n"
               << L"  Method: " << TaskbarMethodText(method) << L'\n'
               << L"  Button disappeared: "
               << ObservationText(removal_observation) << L'\n'
               << L"  Chrome window remained usable: "
               << ObservationText(usable_observation) << L'\n'
               << L"  WindowTabs remained usable: "
               << ObservationText(window_tabs_observation) << L'\n'
               << L"  Alt+Tab remained usable: "
               << ObservationText(alt_tab_observation) << L'\n'
               << L"  Button restored: "
               << ObservationText(restoration_observation) << L'\n'
               << L"  Visual verdict: " << visual_verdict << L'\n';
    if (logger != nullptr) {
        logger->Info(std::wstring(L"Visual verdict: ") +
                     std::wstring(visual_verdict));
    }

    if (!removal.succeeded || !restoration.succeeded) {
        return 4;
    }
    if (visual_failure) {
        return 7;
    }
    return visual_incomplete ? 8 : 0;
}

}  // namespace

int RunTaskbarExperiment(Logger* const logger) {
    try {
        return RunTaskbarExperimentImpl(logger);
    } catch (const std::exception&) {
        if (logger != nullptr) {
            logger->Error("Unhandled standard exception in the experiment.");
        }
        std::wcerr << L"The experiment stopped because of an unexpected "
                      L"standard exception. Any applied in-scope modification "
                      L"was scheduled for restoration.\n";
        return 6;
    } catch (...) {
        if (logger != nullptr) {
            logger->Error("Unhandled unknown exception in the experiment.");
        }
        std::wcerr << L"The experiment stopped because of an unknown "
                      L"exception. Any applied in-scope modification was "
                      L"scheduled for restoration.\n";
        return 6;
    }
}

}  // namespace ctm
