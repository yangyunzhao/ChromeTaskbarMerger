#include "app_config.h"
#include "app_paths.h"
#include "chrome_window.h"
#include "command_line.h"
#include "ctm/version.h"
#include "fixed_entry_app.h"
#include "logger.h"
#include "restore_command.h"
#include "single_instance.h"
#include "taskbar_experiment.h"
#include "tray_app.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

void PrintHelp() {
    std::wcout
        << ctm::kApplicationName << L" " << ctm::kVersion << L"\n\n"
        << L"Usage:\n"
        << L"  ChromeTaskbarMerger.exe [option]\n\n"
        << L"Options:\n"
        << L"  --help, -h, /?  Show this help text.\n"
        << L"  --version       Show the application version.\n"
        << L"  --autostart     Internal Windows-login startup entry.\n"
        << L"  --list          List and classify Chrome top-level windows.\n"
        << L"  --experiment    Interactively test temporary taskbar removal.\n"
        << L"  --manage        Run the diagnostic console lifecycle monitor.\n"
        << L"  --restore-all   Explicitly restore all identifiable Chrome buttons.\n\n"
        << L"With no option, start the V1 notification-area application.\n"
        << L"ChromeTaskbarMerger.ini beside the executable controls the scan "
           L"interval and Windows-login startup.\n";
}

int ListChromeWindows(ctm::Logger* const logger) {
    const ctm::ChromeWindowEnumerationResult result =
        ctm::EnumerateChromeWindows();
    if (!result.succeeded) {
        const std::wstring message =
            L"Chrome window enumeration failed with Win32 error " +
            std::to_wstring(result.error_code) + L'.';
        std::wcerr << L"Error: " << message << L'\n';
        if (logger != nullptr) {
            logger->Error(message);
        }
        return 3;
    }

    const std::wstring summary = ctm::FormatChromeWindowSummary(result);
    std::wcout << summary << L'\n';
    if (logger != nullptr) {
        logger->Info(L"Chrome window scan completed.");
        logger->Info(summary);
    }

    if (result.chrome_windows.empty()) {
        std::wcout << L"No Chrome top-level window candidates were found.\n";
        return 0;
    }

    for (std::size_t index = 0; index < result.chrome_windows.size(); ++index) {
        const std::wstring report = ctm::FormatChromeWindowRecord(
            result.chrome_windows[index], index + 1);
        std::wcout << L"\n" << report << L'\n';
        if (logger != nullptr) {
            logger->Info(report);
        }
    }
    return 0;
}

[[nodiscard]] std::vector<std::wstring_view> BuildArgumentViews(
    const int argument_count,
    wchar_t* const arguments[]) {
    std::vector<std::wstring_view> views;
    if (argument_count <= 1) {
        return views;
    }

    views.reserve(static_cast<std::size_t>(argument_count - 1));
    for (int index = 1; index < argument_count; ++index) {
        views.emplace_back(arguments[index]);
    }
    return views;
}

[[nodiscard]] bool AcquireExclusiveDiagnosticInstance(
    ctm::SingleInstanceGuard* const guard,
    ctm::Logger* const logger) {
    DWORD error_code = ERROR_SUCCESS;
    const ctm::SingleInstanceStatus status =
        guard->Acquire(ctm::kSingletonName, &error_code);
    if (status == ctm::SingleInstanceStatus::Primary) {
        return true;
    }
    const std::wstring message =
        status == ctm::SingleInstanceStatus::Existing
            ? L"Another ChromeTaskbarMerger instance is already running."
            : L"Creating the single-instance mutex failed with Win32 error " +
                  std::to_wstring(error_code) + L'.';
    std::wcerr << L"Error: " << message << L'\n';
    if (logger != nullptr) {
        logger->Error(message);
    }
    return false;
}

[[nodiscard]] ctm::AppConfigLoadResult LoadConfiguration(
    ctm::Logger* const logger,
    const bool show_warnings,
    std::filesystem::path* const configuration_path) {
    std::wstring path_error;
    const std::filesystem::path path =
        ctm::GetConfigurationPath(&path_error);
    if (configuration_path != nullptr) {
        *configuration_path = path;
    }
    ctm::AppConfigLoadResult result;
    if (path.empty()) {
        result.read_succeeded = false;
        result.warnings.push_back(path_error);
    } else {
        result = ctm::LoadAppConfig(path);
    }
    for (const std::wstring& warning : result.warnings) {
        if (show_warnings) {
            std::wcerr << L"Warning: " << warning << L'\n';
        }
        if (logger != nullptr) {
            logger->Warning(warning);
        }
    }
    if (logger != nullptr) {
        logger->Info(
            L"Configuration scan_interval_ms=" +
            std::to_wstring(result.config.scan_interval.count()) +
            L"; start_with_windows=" +
            (result.config.start_with_windows ? L"true" : L"false") + L'.');
    }
    return result;
}

int RunRestoreAllCommand(
    ctm::Logger* const logger,
    const std::filesystem::path& recovery_path) {
    ctm::SingleInstanceGuard guard;
    DWORD mutex_error = ERROR_SUCCESS;
    const ctm::SingleInstanceStatus status =
        guard.Acquire(ctm::kSingletonName, &mutex_error);
    if (status == ctm::SingleInstanceStatus::Error) {
        std::wcerr << L"Error: single-instance mutex failed (Win32 "
                   << mutex_error << L").\n";
        return 4;
    }
    if (status == ctm::SingleInstanceStatus::Existing) {
        DWORD notification_error = ERROR_SUCCESS;
        const bool restored = ctm::NotifyExistingTrayInstance(
            ctm::ExistingInstanceCommand::RestoreAll,
            true,
            &notification_error);
        if (!restored) {
            std::wcerr
                << L"Error: the running instance did not confirm restoration "
                   L"(Win32 "
                << notification_error << L").\n";
            return 5;
        }
        std::wcout
            << L"The running tray instance restored all Chrome buttons and "
               L"paused management.\n";
        return 0;
    }
    return ctm::RunStandaloneRestoreAll(logger, recovery_path);
}

int RunApplication(const std::span<const std::wstring_view> arguments,
                   const HINSTANCE instance) {
    ctm::Logger logger;
    std::wstring logger_error;
    const bool logging_available = logger.Initialize(&logger_error);
    if (logging_available) {
        logger.Info(std::string("ChromeTaskbarMerger ") +
                    std::string(ctm::kVersionUtf8) + " started.");
    } else if (!arguments.empty()) {
        std::wcerr << L"Warning: logging is unavailable: " << logger_error
                   << L'\n';
    }
    ctm::Logger* const active_logger =
        logging_available ? &logger : nullptr;

    const ctm::CommandLineOptions options = ctm::ParseCommandLine(arguments);
    std::filesystem::path configuration_path;
    const ctm::AppConfigLoadResult config = LoadConfiguration(
        active_logger, !arguments.empty(), &configuration_path);

    std::wstring recovery_path_error;
    const std::filesystem::path recovery_path =
        ctm::GetRecoveryJournalPath(&recovery_path_error);

    switch (options.command) {
        case ctm::Command::Run:
        case ctm::Command::AutoStart: {
            if (recovery_path.empty()) {
                if (active_logger != nullptr) {
                    active_logger->Error(recovery_path_error);
                }
                return 5;
            }
            std::wstring executable_path_error;
            const std::filesystem::path executable_path =
                ctm::GetExecutablePath(&executable_path_error);
            if (executable_path.empty() && active_logger != nullptr) {
                active_logger->Error(executable_path_error);
            }
            return ctm::RunTrayApplication(
                instance,
                active_logger,
                config.config,
                recovery_path,
                configuration_path,
                executable_path,
                options.command == ctm::Command::AutoStart);
        }

        case ctm::Command::ListWindows:
            return ListChromeWindows(active_logger);

        case ctm::Command::Experiment: {
            ctm::SingleInstanceGuard guard;
            if (!AcquireExclusiveDiagnosticInstance(&guard, active_logger)) {
                return 4;
            }
            return ctm::RunTaskbarExperiment(active_logger);
        }

        case ctm::Command::Manage: {
            if (recovery_path.empty()) {
                std::wcerr << L"Error: " << recovery_path_error << L'\n';
                return 5;
            }
            ctm::SingleInstanceGuard guard;
            if (!AcquireExclusiveDiagnosticInstance(&guard, active_logger)) {
                return 4;
            }
            return ctm::RunFixedEntryApplication(
                active_logger, config.config, recovery_path);
        }

        case ctm::Command::RestoreAll:
            if (recovery_path.empty()) {
                std::wcerr << L"Error: " << recovery_path_error << L'\n';
                return 5;
            }
            return RunRestoreAllCommand(active_logger, recovery_path);

        case ctm::Command::ShowHelp:
            PrintHelp();
            if (active_logger != nullptr) {
                active_logger->Info("Help requested.");
            }
            return 0;

        case ctm::Command::ShowVersion:
            std::wcout << ctm::kApplicationName << L" " << ctm::kVersion
                       << L'\n';
            if (active_logger != nullptr) {
                active_logger->Info("Version requested.");
            }
            return 0;

        case ctm::Command::Invalid:
            std::wcerr << L"Error: " << options.error_message
                       << L"\nRun with --help for usage.\n";
            if (active_logger != nullptr) {
                active_logger->Error("Invalid command-line arguments.");
            }
            return 2;
    }
    return 2;
}

void AttachParentConsoleForDiagnostics() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE &&
        GetLastError() != ERROR_ACCESS_DENIED) {
        return;
    }
    FILE* stream = nullptr;
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stderr));
    static_cast<void>(freopen_s(&stream, "CONIN$", "r", stdin));
    std::ios::sync_with_stdio(true);
    std::wcout.clear();
    std::wcerr.clear();
    std::wcin.clear();
}

}  // namespace

int wmain(const int argument_count, wchar_t* const arguments[]) {
    const std::vector<std::wstring_view> views =
        BuildArgumentViews(argument_count, arguments);
    return RunApplication(views, GetModuleHandleW(nullptr));
}

int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE,
                    PWSTR,
                    const int) {
    int argument_count = 0;
    wchar_t** const arguments =
        CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return 1;
    }

    const std::vector<std::wstring_view> views =
        BuildArgumentViews(argument_count, arguments);
    if (!views.empty()) {
        AttachParentConsoleForDiagnostics();
    }
    const int exit_code = RunApplication(views, instance);
    LocalFree(arguments);
    return exit_code;
}
