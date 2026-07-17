#pragma once

#include <filesystem>
#include <string>

namespace ctm {

[[nodiscard]] std::filesystem::path GetExecutablePath(
    std::wstring* error_message);
[[nodiscard]] std::filesystem::path GetConfigurationPath(
    std::wstring* error_message);
[[nodiscard]] std::filesystem::path GetRecoveryJournalPath(
    std::wstring* error_message);
[[nodiscard]] std::filesystem::path GetGroupRecoveryJournalPath(
    std::wstring* error_message);

}  // namespace ctm
