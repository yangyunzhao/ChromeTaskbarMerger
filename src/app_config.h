#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

inline constexpr std::chrono::milliseconds kDefaultScanInterval{2000};
inline constexpr std::chrono::milliseconds kDefaultWindowTabsCheckInterval{
    3000};
inline constexpr std::chrono::milliseconds kMinimumScanInterval{500};
inline constexpr std::chrono::milliseconds kMaximumScanInterval{60000};
inline constexpr std::chrono::milliseconds kMinimumWindowTabsCheckInterval{
    500};
inline constexpr std::chrono::milliseconds kMaximumWindowTabsCheckInterval{
    60000};
inline constexpr int kDefaultTabStripWidthPercent = 60;
inline constexpr int kMinimumTabStripWidthPercent = 25;
inline constexpr int kMaximumTabStripWidthPercent = 100;
inline constexpr int kDefaultTabWidthPixels = 180;
inline constexpr int kMinimumTabWidthPixels = 80;
inline constexpr int kMaximumTabWidthPixels = 400;

enum class TabProvider {
    BuiltIn,
    WindowTabs,
};

enum class TabStripAlignment {
    Left,
    Center,
    Right,
};

struct AppConfig {
    std::chrono::milliseconds scan_interval = kDefaultScanInterval;
    std::chrono::milliseconds windowtabs_check_interval =
        kDefaultWindowTabsCheckInterval;
    bool start_with_windows = false;
    TabProvider tab_provider = TabProvider::BuiltIn;
    TabStripAlignment tab_strip_alignment = TabStripAlignment::Center;
    int tab_strip_width_percent = kDefaultTabStripWidthPercent;
    int tab_width_pixels = kDefaultTabWidthPixels;
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

[[nodiscard]] AppConfigSaveResult SaveTabProviderSetting(
    const std::filesystem::path& path,
    TabProvider provider);

[[nodiscard]] std::string_view TabProviderConfigValue(
    TabProvider provider) noexcept;
[[nodiscard]] bool TabProviderSupportsInMemoryNameEditing(
    TabProvider provider) noexcept;
[[nodiscard]] std::wstring_view TabProviderDisplayName(
    TabProvider provider) noexcept;

}  // namespace ctm
