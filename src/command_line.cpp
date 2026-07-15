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

    if (option == L"--list") {
        return {.command = Command::ListWindows};
    }

    return {
        .command = Command::Invalid,
        .error_message = L"Unknown option: " + std::wstring(option),
    };
}

}  // namespace ctm
