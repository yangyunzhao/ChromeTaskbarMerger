#include "command_line.h"

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

}  // namespace

int main() {
    TestEmptyArgumentsRunTheApplication();
    TestHelpAliases();
    TestVersionOption();
    TestUnknownOption();
    TestMultipleOptions();

    if (failures != 0) {
        std::cerr << failures << " command-line test(s) failed.\n";
        return 1;
    }

    std::cout << "All command-line tests passed.\n";
    return 0;
}
