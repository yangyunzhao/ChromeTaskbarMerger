#pragma once

#include <span>
#include <string>
#include <string_view>

namespace ctm {

enum class Command {
    Run,
    ShowHelp,
    ShowVersion,
    Invalid,
};

struct CommandLineOptions {
    Command command = Command::Run;
    std::wstring error_message;
};

[[nodiscard]] CommandLineOptions ParseCommandLine(
    std::span<const std::wstring_view> arguments);

}  // namespace ctm
