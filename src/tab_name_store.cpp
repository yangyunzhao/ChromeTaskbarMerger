#include "tab_name_store.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace ctm {
namespace {

constexpr std::string_view kHeader = "ChromeTaskbarMergerTabNames\t1";
constexpr std::uintmax_t kMaximumFileSize = 256U * 1024U;
constexpr std::size_t kMaximumRules = 256;

[[nodiscard]] std::string WideToUtf8(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string encoded(static_cast<std::size_t>(required), '\0');
    return WideCharToMultiByte(
               CP_UTF8,
               WC_ERR_INVALID_CHARS,
               value.data(),
               static_cast<int>(value.size()),
               encoded.data(),
               required,
               nullptr,
               nullptr) == required
               ? encoded
               : std::string{};
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring decoded(static_cast<std::size_t>(required), L'\0');
    return MultiByteToWideChar(
               CP_UTF8,
               MB_ERR_INVALID_CHARS,
               value.data(),
               static_cast<int>(value.size()),
               decoded.data(),
               required) == required
               ? decoded
               : std::wstring{};
}

[[nodiscard]] bool IdIsValid(const std::string_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(
               id.begin(), id.end(), [](const unsigned char character) {
                   return (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') ||
                          character == '-' || character == '_';
               });
}

[[nodiscard]] bool TextIsValid(const std::wstring_view value,
                               const std::size_t maximum) {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    const std::string encoded = WideToUtf8(value);
    return !encoded.empty() &&
           encoded.find_first_of("\t\r\n") == std::string::npos;
}

[[nodiscard]] bool RuleIsValid(const TabNameRule& rule) {
    return IdIsValid(rule.id) &&
           TextIsValid(rule.process_path, 1024) &&
           TextIsValid(rule.class_name, 255) &&
           TextIsValid(rule.exact_window_title, 1024) &&
           TextIsValid(rule.display_name, 128);
}

[[nodiscard]] std::vector<std::string_view> Split(
    const std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t separator = line.find('\t', start);
        if (separator == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 1;
    }
    return fields;
}

[[nodiscard]] bool EqualInsensitive(const std::wstring_view left,
                                    const std::wstring_view right) {
    return left.size() == right.size() &&
           _wcsnicmp(left.data(), right.data(), left.size()) == 0;
}

[[nodiscard]] bool RuleMatches(const TabNameRule& rule,
                               const ChromeWindowSnapshot& window) {
    return EqualInsensitive(rule.process_path, window.process_path) &&
           EqualInsensitive(rule.class_name, window.class_name) &&
           rule.exact_window_title == window.title;
}

}  // namespace

std::string SerializeTabNameRules(
    const std::span<const TabNameRule> rules) {
    if (rules.size() > kMaximumRules) {
        return {};
    }
    std::unordered_set<std::string> ids;
    std::string serialized(kHeader);
    serialized.push_back('\n');
    for (const TabNameRule& rule : rules) {
        if (!RuleIsValid(rule) || !ids.insert(rule.id).second) {
            return {};
        }
        serialized += "rule\t" + rule.id + "\t" +
                      WideToUtf8(rule.process_path) + "\t" +
                      WideToUtf8(rule.class_name) + "\t" +
                      WideToUtf8(rule.exact_window_title) + "\t" +
                      WideToUtf8(rule.display_name) + "\n";
    }
    return serialized.size() <= kMaximumFileSize ? serialized
                                                 : std::string{};
}

TabNameParseResult ParseTabNameRules(
    const std::string_view serialized) {
    TabNameParseResult result;
    if (serialized.empty() || serialized.size() > kMaximumFileSize) {
        result.error_message = L"The tab-name file is empty or too large.";
        return result;
    }
    std::size_t start = 0;
    std::size_t line_number = 0;
    std::unordered_set<std::string> ids;
    while (start <= serialized.size()) {
        const std::size_t newline = serialized.find('\n', start);
        std::string_view line = newline == std::string_view::npos
                                    ? serialized.substr(start)
                                    : serialized.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        ++line_number;
        if (line_number == 1) {
            if (line != kHeader) {
                result.error_message =
                    L"The tab-name file header or version is invalid.";
                return result;
            }
        } else if (!line.empty()) {
            if (result.rules.size() >= kMaximumRules) {
                result.rules.clear();
                result.error_message = L"The tab-name rule limit was exceeded.";
                return result;
            }
            const std::vector fields = Split(line);
            if (fields.size() != 6 || fields[0] != "rule") {
                result.rules.clear();
                result.error_message =
                    L"Tab-name rule line " + std::to_wstring(line_number) +
                    L" has an invalid shape.";
                return result;
            }
            TabNameRule rule{
                .id = std::string(fields[1]),
                .process_path = Utf8ToWide(fields[2]),
                .class_name = Utf8ToWide(fields[3]),
                .exact_window_title = Utf8ToWide(fields[4]),
                .display_name = Utf8ToWide(fields[5]),
            };
            if (!RuleIsValid(rule) || !ids.insert(rule.id).second) {
                result.rules.clear();
                result.error_message =
                    L"Tab-name rule line " + std::to_wstring(line_number) +
                    L" is invalid or duplicated.";
                return result;
            }
            result.rules.push_back(std::move(rule));
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    result.succeeded = true;
    return result;
}

TabNameLoadResult LoadTabNameRules(const std::filesystem::path& path) {
    TabNameLoadResult result;
    if (path.empty()) {
        result.error_message = L"The tab-name path is unavailable.";
        return result;
    }
    std::error_code error;
    result.file_found = std::filesystem::exists(path, error);
    if (error) {
        result.error_message = L"Querying the tab-name file failed.";
        return result;
    }
    if (!result.file_found) {
        result.succeeded = true;
        return result;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumFileSize) {
        result.error_message = L"The tab-name file is unavailable or too large.";
        return result;
    }
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        result.error_message = L"Opening the tab-name file failed.";
        return result;
    }
    const std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        result.error_message = L"Reading the tab-name file failed.";
        return result;
    }
    TabNameParseResult parsed = ParseTabNameRules(text);
    result.succeeded = parsed.succeeded;
    result.rules = std::move(parsed.rules);
    result.error_message = std::move(parsed.error_message);
    return result;
}

bool SaveTabNameRulesAtomically(
    const std::filesystem::path& path,
    const std::span<const TabNameRule> rules,
    std::wstring* const error_message) {
    const std::string serialized = SerializeTabNameRules(rules);
    if (path.empty() || serialized.empty()) {
        if (error_message != nullptr) {
            *error_message = L"The tab-name path or rule set is invalid.";
        }
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = L"Creating the tab-name directory failed.";
        }
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64());
    {
        std::ofstream output(
            temporary, std::ios::out | std::ios::binary | std::ios::trunc);
        output.write(
            serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            if (error_message != nullptr) {
                *error_message = L"Writing the tab-name file failed.";
            }
            return false;
        }
    }
    if (MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD move_error = GetLastError();
        std::filesystem::remove(temporary, error);
        if (error_message != nullptr) {
            *error_message =
                L"Replacing the tab-name file failed with Win32 error " +
                std::to_wstring(move_error) + L'.';
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

std::vector<std::wstring> ResolveTabDisplayNames(
    const std::span<const TabNameRule> rules,
    const std::span<const ChromeWindowSnapshot> windows) {
    std::vector<std::wstring> names;
    names.reserve(windows.size());
    for (const ChromeWindowSnapshot& window : windows) {
        names.push_back(window.title);
    }

    std::vector<std::vector<std::size_t>> candidates(windows.size());
    for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
        std::vector<std::size_t> matching_windows;
        for (std::size_t window_index = 0;
             window_index < windows.size();
             ++window_index) {
            if (RuleMatches(rules[rule_index], windows[window_index])) {
                matching_windows.push_back(window_index);
            }
        }
        if (matching_windows.size() == 1) {
            candidates[matching_windows.front()].push_back(rule_index);
        }
    }
    for (std::size_t window_index = 0;
         window_index < windows.size();
         ++window_index) {
        if (candidates[window_index].size() == 1) {
            names[window_index] =
                rules[candidates[window_index].front()].display_name;
        }
    }
    return names;
}

}  // namespace ctm
