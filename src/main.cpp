#include "command_line.h"
#include "ctm/version.h"
#include "logger.h"

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
        << L"  --version       Show the application version.\n\n"
        << L"Phase 0 only provides the project scaffold and diagnostics.\n"
        << L"It does not modify Chrome windows or taskbar state.\n";
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
                       << L"\nPhase 0 scaffold is ready. No taskbar changes were made.\n";
            if (logging_available) {
                std::wcout << L"Log: " << logger.log_path().wstring() << L'\n';
                logger.Info("Phase 0 startup completed; no window APIs were invoked.");
            }
            return 0;

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
