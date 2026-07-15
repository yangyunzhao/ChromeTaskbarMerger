#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ctm {

inline constexpr std::chrono::milliseconds kDefaultScanInterval{2000};
inline constexpr std::chrono::milliseconds kMinimumScanInterval{500};
inline constexpr std::chrono::milliseconds kMaximumScanInterval{60000};

struct AppConfig {
    std::chrono::milliseconds scan_interval = kDefaultScanInterval;
};

struct AppConfigLoadResult {
    AppConfig config;
    bool file_found = false;
    bool read_succeeded = true;
    std::vector<std::wstring> warnings;
};

[[nodiscard]] AppConfigLoadResult LoadAppConfig(
    const std::filesystem::path& path);

}  // namespace ctm
