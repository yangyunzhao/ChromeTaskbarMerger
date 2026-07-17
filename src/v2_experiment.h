#pragma once

#include "logger.h"

#include <Windows.h>

#include <filesystem>

namespace ctm {

[[nodiscard]] int RunV2Experiment(
    HINSTANCE instance,
    Logger* logger,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path);

}  // namespace ctm
