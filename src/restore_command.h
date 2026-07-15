#pragma once

#include <filesystem>

namespace ctm {

class Logger;

[[nodiscard]] int RunStandaloneRestoreAll(
    Logger* logger,
    const std::filesystem::path& recovery_journal_path);

}  // namespace ctm
