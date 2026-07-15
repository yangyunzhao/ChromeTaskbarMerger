#include "chrome_window.h"
#include "command_line.h"
#include "ctm/version.h"
#include "logger.h"
#include "taskbar_experiment.h"

#include <Windows.h>
#include <shellapi.h>

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
        << L"  --list          List and classify Chrome top-level windows.\n"
        << L"  --experiment    Interactively test temporary taskbar removal.\n\n"
        << L"--list is read-only. --experiment modifies one selected window "
           L"only after explicit confirmation and then restores it.\n";
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

int RunApplication(const std::span<const std::wstring_view> arguments) {
    ctm::Logger logger;
    std::wstring logger_error;
    const bool logging_available = logger.Initialize(&logger_error);
    if (logging_available) {
        logger.Info(std::string("ChromeTaskbarMerger ") +
                    std::string(ctm::kVersionUtf8) + " started.");
    } else {
        std::wcerr << L"Warning: logging is unavailable: " << logger_error
                   << L'\n';
    }

    const ctm::CommandLineOptions options = ctm::ParseCommandLine(arguments);
    switch (options.command) {
        case ctm::Command::Run:
            std::wcout << ctm::kApplicationName << L" " << ctm::kVersion
                       << L"\nPhase 1 diagnostics are ready. No taskbar changes were made.\n";
            if (logging_available) {
                std::wcout << L"Log: " << logger.log_path().wstring() << L'\n';
                logger.Info("Phase 1 startup completed; no taskbar APIs were invoked.");
            }
            return 0;

        case ctm::Command::ListWindows:
            return ListChromeWindows(logging_available ? &logger : nullptr);

        case ctm::Command::Experiment:
            return ctm::RunTaskbarExperiment(
                logging_available ? &logger : nullptr);

        case ctm::Command::ShowHelp:
            PrintHelp();
            if (logging_available) {
                logger.Info("Help requested.");
            }
            return 0;

        case ctm::Command::ShowVersion:
            std::wcout << ctm::kApplicationName << L" " << ctm::kVersion
                       << L'\n';
            if (logging_available) {
                logger.Info("Version requested.");
            }
            return 0;

        case ctm::Command::Invalid:
            std::wcerr << L"Error: " << options.error_message
                       << L"\nRun with --help for usage.\n";
            if (logging_available) {
                logger.Error("Invalid command-line arguments.");
            }
            return 2;
    }

    return 2;
}

}  // namespace

int wmain(const int argument_count, wchar_t* const arguments[]) {
    const std::vector<std::wstring_view> views =
        BuildArgumentViews(argument_count, arguments);
    return RunApplication(views);
}

int WINAPI wWinMain(HINSTANCE,
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
    const int exit_code = RunApplication(views);
    LocalFree(arguments);
    return exit_code;
}
