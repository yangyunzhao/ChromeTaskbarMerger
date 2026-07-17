#pragma once

#include <filesystem>
#include <string>

namespace ctm {

class Logger;

struct StartupGroupRecoveryResult {
    bool succeeded = false;
    bool recovery_attempted = false;
    std::wstring error_message;
};

[[nodiscard]] StartupGroupRecoveryResult RestorePreviousGroupSession(
    Logger* logger,
    const std::filesystem::path& group_recovery_journal_path);

[[nodiscard]] int RunStandaloneRestoreAll(
    Logger* logger,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path);

}  // namespace ctm
