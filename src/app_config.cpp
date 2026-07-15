#include "app_config.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>

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

}  // namespace ctm
