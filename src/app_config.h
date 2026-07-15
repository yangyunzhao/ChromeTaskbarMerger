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
    bool start_with_windows = false;
};

struct AppConfigLoadResult {
    AppConfig config;
    bool file_found = false;
    bool read_succeeded = true;
    std::vector<std::wstring> warnings;
};

struct AppConfigSaveResult {
    bool succeeded = false;
    std::wstring error_message;
};

[[nodiscard]] AppConfigLoadResult LoadAppConfig(
    const std::filesystem::path& path);

[[nodiscard]] AppConfigSaveResult SaveStartWithWindowsSetting(
    const std::filesystem::path& path,
    bool enabled);

}  // namespace ctm
