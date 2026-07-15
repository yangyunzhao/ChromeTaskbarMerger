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

[[nodiscard]] std::string DefaultConfigurationText() {
    return "; ChromeTaskbarMerger portable configuration\r\n"
           "; Changes take effect after the application is restarted.\r\n"
           "\r\n"
           "[ChromeTaskbarMerger]\r\n"
           "; Allowed range: 500 to 60000 milliseconds. Default: 2000.\r\n"
           "scan_interval_ms=2000\r\n";
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

[[nodiscard]] std::string UpdateStartWithWindowsText(
    const std::string_view content,
    const bool enabled) {
    const std::string newline = content.find("\r\n") != std::string_view::npos
                                    ? "\r\n"
                                    : "\n";
    const std::string setting = StartWithWindowsLine(enabled);
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
                output.push_back(setting);
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
                if (key == "start_with_windows") {
                    if (!setting_written) {
                        output.push_back(setting);
                        setting_written = true;
                    }
                    continue;
                }
            }
        }
        output.push_back(line);
    }

    if (in_supported_section && !setting_written) {
        output.push_back(setting);
        setting_written = true;
    }
    if (!supported_section_seen) {
        if (!output.empty() && !output.back().empty()) {
            output.emplace_back();
        }
        output.emplace_back("[ChromeTaskbarMerger]");
        output.push_back(setting);
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
    bool start_with_windows_seen = false;
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

        if (key != "scan_interval_ms") {
            result.warnings.push_back(LineWarning(
                line_number, L"unknown key; the line was ignored."));
            continue;
        }
        if (scan_interval_seen) {
            result.warnings.push_back(LineWarning(
                line_number,
                L"duplicate scan_interval_ms; the last valid value is used."));
        }
        scan_interval_seen = true;

        std::int64_t milliseconds = 0;
        const auto parsed = std::from_chars(
            value.data(), value.data() + value.size(), milliseconds);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size() ||
            milliseconds < kMinimumScanInterval.count() ||
            milliseconds > kMaximumScanInterval.count()) {
            result.warnings.push_back(LineWarning(
                line_number,
                L"scan_interval_ms must be an integer from 500 to 60000; "
                L"the previous value is kept."));
            continue;
        }
        result.config.scan_interval =
            std::chrono::milliseconds(milliseconds);
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

    const std::string updated =
        UpdateStartWithWindowsText(content, enabled);
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

}  // namespace ctm
