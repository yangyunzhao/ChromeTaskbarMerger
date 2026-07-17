#include "app_config.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctm {
namespace {

[[nodiscard]] std::string_view Trim(const std::string_view value) {
    const auto is_space = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [is_space](const char character) {
            return is_space(static_cast<unsigned char>(character));
        });
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [is_space](const char character) {
            return is_space(static_cast<unsigned char>(character));
        }).base();
    return first < last ? std::string_view(first, last) : std::string_view{};
}

[[nodiscard]] std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return lowered;
}

[[nodiscard]] std::wstring LineWarning(const std::size_t line,
                                       const std::wstring_view message) {
    return L"Configuration line " + std::to_wstring(line) + L": " +
           std::wstring(message);
}

[[nodiscard]] std::string StartWithWindowsLine(const bool enabled) {
    return std::string("start_with_windows=") +
           (enabled ? "true" : "false");
}

[[nodiscard]] std::string TabProviderLine(const TabProvider provider) {
    return std::string("tab_provider=") +
           std::string(TabProviderConfigValue(provider));
}

[[nodiscard]] std::string ProfileTabNamePersistenceLine(
    const bool enabled) {
    return std::string("persist_tab_names_by_profile=") +
           (enabled ? "true" : "false");
}

[[nodiscard]] std::string DefaultConfigurationText() {
    return "; ChromeTaskbarMerger portable configuration\r\n"
           "; Changes take effect after the application is restarted.\r\n"
           "\r\n"
           "[ChromeTaskbarMerger]\r\n"
           "; Allowed range: 500 to 60000 milliseconds. Default: 2000.\r\n"
           "scan_interval_ms=2000\r\n"
           "; Built-in (default) or WindowTabs. Restart after changing.\r\n"
           "tab_provider=builtin\r\n"
           "; Remember built-in custom names by local Chrome profile.\r\n"
           "; Disabled by default; restart after changing.\r\n"
           "persist_tab_names_by_profile=false\r\n"
           "; WindowTabs process check while that provider is selected.\r\n"
           "windowtabs_check_interval_ms=3000\r\n"
           "; Built-in tab strip: left, center, or right.\r\n"
           "tab_strip_alignment=center\r\n"
           "; Built-in strip width: 25 to 100 percent.\r\n"
           "tab_strip_width_percent=60\r\n"
           "; Built-in maximum tab width: 80 to 400 logical pixels.\r\n"
           "tab_width_px=180\r\n";
}

[[nodiscard]] std::vector<std::string> SplitLines(
    const std::string_view content) {
    std::vector<std::string> lines;
    std::size_t position = 0;
    while (position < content.size()) {
        const std::size_t newline = content.find('\n', position);
        const std::size_t end = newline == std::string_view::npos
                                    ? content.size()
                                    : newline;
        std::string line(content.substr(position, end - position));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (newline == std::string_view::npos) {
            break;
        }
        position = newline + 1;
    }
    return lines;
}

[[nodiscard]] std::string UpdateSettingText(
    const std::string_view content,
    const std::string_view key_to_update,
    const std::string_view setting) {
    const std::string newline = content.find("\r\n") != std::string_view::npos
                                    ? "\r\n"
                                    : "\n";
    const std::vector<std::string> lines = SplitLines(content);
    std::vector<std::string> output;
    output.reserve(lines.size() + 3U);

    bool in_supported_section = false;
    bool supported_section_seen = false;
    bool setting_written = false;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        std::string_view comparable = line;
        if (index == 0 && comparable.starts_with("\xEF\xBB\xBF")) {
            comparable.remove_prefix(3);
        }
        const std::string_view trimmed = Trim(comparable);

        if (trimmed.size() >= 2 && trimmed.front() == '[' &&
            trimmed.back() == ']') {
            if (in_supported_section && !setting_written) {
                output.emplace_back(setting);
                setting_written = true;
            }
            const std::string section = LowerAscii(
                Trim(trimmed.substr(1, trimmed.size() - 2)));
            in_supported_section = section == "chrometaskbarmerger";
            supported_section_seen =
                supported_section_seen || in_supported_section;
            output.push_back(line);
            continue;
        }

        if (in_supported_section) {
            const std::size_t separator = trimmed.find('=');
            if (separator != std::string_view::npos) {
                const std::string key =
                    LowerAscii(Trim(trimmed.substr(0, separator)));
                if (key == key_to_update) {
                    if (!setting_written) {
                        output.emplace_back(setting);
                        setting_written = true;
                    }
                    continue;
                }
            }
        }
        output.push_back(line);
    }

    if (in_supported_section && !setting_written) {
        output.emplace_back(setting);
        setting_written = true;
    }
    if (!supported_section_seen) {
        if (!output.empty() && !output.back().empty()) {
            output.emplace_back();
        }
        output.emplace_back("[ChromeTaskbarMerger]");
        output.emplace_back(setting);
    }

    std::string updated;
    for (const std::string& line : output) {
        updated += line;
        updated += newline;
    }
    return updated;
}

[[nodiscard]] AppConfigSaveResult SaveFailure(std::wstring message) {
    return {
        .succeeded = false,
        .error_message = std::move(message),
    };
}

[[nodiscard]] AppConfigSaveResult SaveSetting(
    const std::filesystem::path& path,
    const std::string_view key,
    const std::string_view setting) {
    if (path.empty()) {
        return SaveFailure(L"The configuration path is unavailable.");
    }

    std::error_code filesystem_error;
    const bool file_exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        return SaveFailure(
            L"Unable to query the configuration file before saving.");
    }

    std::string content;
    if (file_exists) {
        const std::uintmax_t size =
            std::filesystem::file_size(path, filesystem_error);
        if (filesystem_error || size > 64U * 1024U) {
            return SaveFailure(
                L"The configuration file is unavailable or too large to "
                L"update safely.");
        }
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input.is_open()) {
            return SaveFailure(
                L"Unable to open the configuration file for reading.");
        }
        content.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
        if (input.bad()) {
            return SaveFailure(L"Reading the configuration file failed.");
        }
    } else {
        content = DefaultConfigurationText();
    }

    const std::string updated = UpdateSettingText(content, key, setting);
    std::filesystem::path temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetCurrentThreadId()) + L"-" +
                 std::to_wstring(GetTickCount64());

    {
        std::ofstream output(
            temporary, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return SaveFailure(
                L"Unable to create a temporary configuration file.");
        }
        output.write(
            updated.data(), static_cast<std::streamsize>(updated.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            DeleteFileW(temporary.c_str());
            return SaveFailure(
                L"Writing the temporary configuration file failed.");
        }
    }

    if (MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD error_code = GetLastError();
        DeleteFileW(temporary.c_str());
        return SaveFailure(
            L"Replacing the configuration file failed with Win32 error " +
            std::to_wstring(error_code) + L'.');
    }
    return {.succeeded = true};
}

}  // namespace

AppConfigLoadResult LoadAppConfig(const std::filesystem::path& path) {
    AppConfigLoadResult result;
    std::error_code filesystem_error;
    result.file_found = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        result.read_succeeded = false;
        result.warnings.push_back(
            L"Unable to query the configuration file; defaults are used.");
        return result;
    }
    if (!result.file_found) {
        return result;
    }

    const std::uintmax_t size = std::filesystem::file_size(
        path, filesystem_error);
    if (filesystem_error || size > 64U * 1024U) {
        result.read_succeeded = false;
        result.warnings.push_back(
            L"The configuration file is unavailable or too large; defaults "
            L"are used.");
        return result;
    }

    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        result.read_succeeded = false;
        result.warnings.push_back(
            L"Unable to open the configuration file; defaults are used.");
        return result;
    }

    bool in_supported_section = false;
    bool scan_interval_seen = false;
    bool windowtabs_check_interval_seen = false;
    bool start_with_windows_seen = false;
    bool tab_provider_seen = false;
    bool profile_tab_name_persistence_seen = false;
    bool tab_strip_alignment_seen = false;
    bool tab_strip_width_percent_seen = false;
    bool tab_width_pixels_seen = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }

        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == ';' ||
            trimmed.front() == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            const std::string section = LowerAscii(
                Trim(trimmed.substr(1, trimmed.size() - 2)));
            in_supported_section = section == "chrometaskbarmerger";
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string_view::npos) {
            result.warnings.push_back(LineWarning(
                line_number, L"expected key=value; the line was ignored."));
            continue;
        }
        if (!in_supported_section) {
            continue;
        }

        const std::string key = LowerAscii(Trim(trimmed.substr(0, separator)));
        const std::string_view value = Trim(trimmed.substr(separator + 1));
        if (key == "start_with_windows") {
            if (start_with_windows_seen) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"duplicate start_with_windows; the last valid value is "
                    L"used."));
            }
            start_with_windows_seen = true;
            const std::string lowered_value = LowerAscii(value);
            if (lowered_value == "true") {
                result.config.start_with_windows = true;
            } else if (lowered_value == "false") {
                result.config.start_with_windows = false;
            } else {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"start_with_windows must be true or false; the previous "
                    L"value is kept."));
            }
            continue;
        }

        if (key == "tab_provider") {
            if (tab_provider_seen) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"duplicate tab_provider; the last valid value is used."));
            }
            tab_provider_seen = true;
            const std::string lowered_value = LowerAscii(value);
            if (lowered_value == "builtin") {
                result.config.tab_provider = TabProvider::BuiltIn;
            } else if (lowered_value == "windowtabs") {
                result.config.tab_provider = TabProvider::WindowTabs;
            } else {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"tab_provider must be builtin or windowtabs; the previous value is kept."));
            }
            continue;
        }

        if (key == "persist_tab_names_by_profile") {
            if (profile_tab_name_persistence_seen) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"duplicate persist_tab_names_by_profile; the last valid value is used."));
            }
            profile_tab_name_persistence_seen = true;
            const std::string lowered_value = LowerAscii(value);
            if (lowered_value == "true") {
                result.config.persist_tab_names_by_profile = true;
            } else if (lowered_value == "false") {
                result.config.persist_tab_names_by_profile = false;
            } else {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"persist_tab_names_by_profile must be true or false; the previous value is kept."));
            }
            continue;
        }

        if (key == "tab_strip_alignment") {
            if (tab_strip_alignment_seen) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"duplicate tab_strip_alignment; the last valid value is used."));
            }
            tab_strip_alignment_seen = true;
            const std::string lowered_value = LowerAscii(value);
            if (lowered_value == "left") {
                result.config.tab_strip_alignment = TabStripAlignment::Left;
            } else if (lowered_value == "center") {
                result.config.tab_strip_alignment = TabStripAlignment::Center;
            } else if (lowered_value == "right") {
                result.config.tab_strip_alignment = TabStripAlignment::Right;
            } else {
                result.warnings.push_back(LineWarning(
                    line_number,
                    L"tab_strip_alignment must be left, center, or right; the previous value is kept."));
            }
            continue;
        }

        const bool is_tab_strip_width =
            key == "tab_strip_width_percent";
        const bool is_tab_width = key == "tab_width_px";
        if (is_tab_strip_width || is_tab_width) {
            bool& setting_seen = is_tab_strip_width
                                     ? tab_strip_width_percent_seen
                                     : tab_width_pixels_seen;
            if (setting_seen) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    is_tab_strip_width
                        ? L"duplicate tab_strip_width_percent; the last valid value is used."
                        : L"duplicate tab_width_px; the last valid value is used."));
            }
            setting_seen = true;
            std::int64_t parsed_value = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), parsed_value);
            const int minimum = is_tab_strip_width
                                    ? kMinimumTabStripWidthPercent
                                    : kMinimumTabWidthPixels;
            const int maximum = is_tab_strip_width
                                    ? kMaximumTabStripWidthPercent
                                    : kMaximumTabWidthPixels;
            if (parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size() ||
                parsed_value < minimum || parsed_value > maximum) {
                result.warnings.push_back(LineWarning(
                    line_number,
                    is_tab_strip_width
                        ? L"tab_strip_width_percent must be an integer from 25 to 100; the previous value is kept."
                        : L"tab_width_px must be an integer from 80 to 400; the previous value is kept."));
                continue;
            }
            if (is_tab_strip_width) {
                result.config.tab_strip_width_percent =
                    static_cast<int>(parsed_value);
            } else {
                result.config.tab_width_pixels =
                    static_cast<int>(parsed_value);
            }
            continue;
        }

        const bool is_scan_interval = key == "scan_interval_ms";
        const bool is_windowtabs_check_interval =
            key == "windowtabs_check_interval_ms";
        if (!is_scan_interval && !is_windowtabs_check_interval) {
            result.warnings.push_back(LineWarning(
                line_number, L"unknown key; the line was ignored."));
            continue;
        }
        bool& interval_seen = is_scan_interval
                                  ? scan_interval_seen
                                  : windowtabs_check_interval_seen;
        if (interval_seen) {
            result.warnings.push_back(LineWarning(
                line_number,
                is_scan_interval
                    ? L"duplicate scan_interval_ms; the last valid value is "
                      L"used."
                    : L"duplicate windowtabs_check_interval_ms; the last "
                      L"valid value is used."));
        }
        interval_seen = true;

        std::int64_t milliseconds = 0;
        const auto parsed = std::from_chars(
            value.data(), value.data() + value.size(), milliseconds);
        const std::chrono::milliseconds minimum =
            is_scan_interval ? kMinimumScanInterval
                             : kMinimumWindowTabsCheckInterval;
        const std::chrono::milliseconds maximum =
            is_scan_interval ? kMaximumScanInterval
                             : kMaximumWindowTabsCheckInterval;
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size() ||
            milliseconds < minimum.count() ||
            milliseconds > maximum.count()) {
            result.warnings.push_back(LineWarning(
                line_number,
                is_scan_interval
                    ? L"scan_interval_ms must be an integer from 500 to 60000; "
                      L"the previous value is kept."
                    : L"windowtabs_check_interval_ms must be an integer from "
                      L"500 to 60000; the previous value is kept."));
            continue;
        }
        if (is_scan_interval) {
            result.config.scan_interval =
                std::chrono::milliseconds(milliseconds);
        } else {
            result.config.windowtabs_check_interval =
                std::chrono::milliseconds(milliseconds);
        }
    }

    if (input.bad()) {
        result.read_succeeded = false;
        result.config = {};
        result.warnings.push_back(
            L"Reading the configuration file failed; defaults are used.");
    }
    return result;
}

AppConfigSaveResult SaveStartWithWindowsSetting(
    const std::filesystem::path& path,
    const bool enabled) {
    const std::string setting = StartWithWindowsLine(enabled);
    return SaveSetting(path, "start_with_windows", setting);
}

AppConfigSaveResult SaveTabProviderSetting(
    const std::filesystem::path& path,
    const TabProvider provider) {
    const std::string setting = TabProviderLine(provider);
    return SaveSetting(path, "tab_provider", setting);
}

AppConfigSaveResult SaveProfileTabNamePersistenceSetting(
    const std::filesystem::path& path,
    const bool enabled) {
    const std::string setting = ProfileTabNamePersistenceLine(enabled);
    return SaveSetting(
        path, "persist_tab_names_by_profile", setting);
}

std::string_view TabProviderConfigValue(
    const TabProvider provider) noexcept {
    switch (provider) {
        case TabProvider::BuiltIn:
            return "builtin";
        case TabProvider::WindowTabs:
            return "windowtabs";
    }
    return "builtin";
}

bool TabProviderSupportsInMemoryNameEditing(
    const TabProvider provider) noexcept {
    return provider == TabProvider::BuiltIn;
}

std::wstring_view TabProviderDisplayName(
    const TabProvider provider) noexcept {
    switch (provider) {
        case TabProvider::BuiltIn:
            return L"内置标签";
        case TabProvider::WindowTabs:
            return L"WindowTabs 标签";
    }
    return L"内置标签";
}

}  // namespace ctm
