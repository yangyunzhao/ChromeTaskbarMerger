#include "command_line.h"

namespace ctm {

CommandLineOptions ParseCommandLine(
    const std::span<const std::wstring_view> arguments) {
    if (arguments.empty()) {
        return {};
    }

    if (arguments.size() > 1) {
        return {
            .command = Command::Invalid,
            .error_message = L"Only one command-line option may be specified.",
        };
    }

    const std::wstring_view option = arguments.front();
    if (option == L"--help" || option == L"-h" || option == L"/?") {
        return {.command = Command::ShowHelp};
    }

    if (option == L"--version") {
        return {.command = Command::ShowVersion};
    }

    if (option == L"--autostart") {
        return {.command = Command::AutoStart};
    }

    if (option == L"--list") {
        return {.command = Command::ListWindows};
    }

    if (option == L"--experiment") {
        return {.command = Command::Experiment};
    }

    if (option == L"--manage") {
        return {.command = Command::Manage};
    }

    if (option == L"--restore-all") {
        return {.command = Command::RestoreAll};
    }

    return {
        .command = Command::Invalid,
        .error_message = L"Unknown option: " + std::wstring(option),
    };
}

}  // namespace ctm
