#include "recovery_journal.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace ctm {
namespace {

constexpr std::string_view kHeader = "ChromeTaskbarMergerRecovery\t1";
constexpr std::uintmax_t kMaximumJournalSize = 1024U * 1024U;
constexpr std::size_t kMaximumRecords = 256;

[[nodiscard]] std::string WideToUtf8(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string encoded(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), encoded.data(), required,
        nullptr, nullptr);
    return written == required ? encoded : std::string{};
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring decoded(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), decoded.data(), required);
    return written == required ? decoded : std::wstring{};
}

[[nodiscard]] std::vector<std::string_view> SplitFields(
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

template <typename Integer>
[[nodiscard]] bool ParseInteger(const std::string_view value,
                                const int base,
                                Integer* const parsed_value) {
    Integer parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, base);
    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size()) {
        return false;
    }
    *parsed_value = parsed;
    return true;
}

[[nodiscard]] std::wstring ParseError(const std::size_t line,
                                      const std::wstring_view detail) {
    return L"Recovery journal line " + std::to_wstring(line) + L": " +
           std::wstring(detail);
}

[[nodiscard]] std::string IdentityKey(const TaskbarMutationState& state) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(
               state.identity.hwnd)) +
           ":" + std::to_string(state.identity.process_id) + ":" +
           std::to_string(state.identity.thread_id) + ":" +
           std::to_string(state.identity.process_creation_time) + ":" +
           WideToUtf8(state.identity.class_name);
}

}  // namespace

std::string SerializeRecoveryStates(
    const std::span<const TaskbarMutationState> states) {
    std::ostringstream output;
    output << kHeader << '\n';
    for (const TaskbarMutationState& state : states) {
        if (!state.NeedsRestore()) {
            continue;
        }
        const std::string class_name = WideToUtf8(state.identity.class_name);
        if (class_name.empty() || class_name.find_first_of("\t\r\n") !=
                                      std::string::npos) {
            return {};
        }
        output << "state\t"
               << (state.method == TaskbarMethod::TaskbarList
                       ? "taskbar_list"
                       : "window_style")
               << '\t' << std::hex << std::uppercase
               << reinterpret_cast<std::uintptr_t>(state.identity.hwnd)
               << std::dec << '\t' << state.identity.process_id << '\t'
               << state.identity.thread_id << '\t'
               << state.identity.process_creation_time << '\t'
               << class_name << '\t' << std::hex << std::uppercase
               << static_cast<std::uintptr_t>(state.original_extended_style)
               << '\t'
               << static_cast<std::uintptr_t>(state.applied_extended_style)
               << std::dec << '\n';
    }
    return output.str();
}

RecoveryParseResult ParseRecoveryStates(const std::string_view serialized) {
    RecoveryParseResult result;
    if (serialized.empty() || serialized.size() > kMaximumJournalSize) {
        result.error_message = L"The recovery journal is empty or too large.";
        return result;
    }

    std::size_t line_number = 0;
    std::size_t start = 0;
    bool header_seen = false;
    std::unordered_set<std::string> identities;
    std::vector<TaskbarMutationState> parsed_states;
    while (start <= serialized.size()) {
        const std::size_t newline = serialized.find('\n', start);
        std::string_view line = newline == std::string_view::npos
                                    ? serialized.substr(start)
                                    : serialized.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        ++line_number;
        if (!header_seen) {
            if (line != kHeader) {
                result.error_message =
                    L"The recovery journal header or version is invalid.";
                return result;
            }
            header_seen = true;
        } else if (!line.empty()) {
            if (parsed_states.size() >= kMaximumRecords) {
                result.error_message = L"The recovery journal has too many records.";
                return result;
            }
            const std::vector fields = SplitFields(line);
            if (fields.size() != 9 || fields[0] != "state") {
                result.error_message =
                    ParseError(line_number, L"record shape is invalid.");
                return result;
            }

            TaskbarMutationState state;
            if (fields[1] == "taskbar_list") {
                state.method = TaskbarMethod::TaskbarList;
            } else if (fields[1] == "window_style") {
                state.method = TaskbarMethod::WindowStyle;
            } else {
                result.error_message =
                    ParseError(line_number, L"taskbar method is invalid.");
                return result;
            }

            std::uintptr_t hwnd_value = 0;
            std::uint32_t process_id = 0;
            std::uint32_t thread_id = 0;
            std::uint64_t creation_time = 0;
            std::uintptr_t original_style = 0;
            std::uintptr_t applied_style = 0;
            if (!ParseInteger(fields[2], 16, &hwnd_value) ||
                !ParseInteger(fields[3], 10, &process_id) ||
                !ParseInteger(fields[4], 10, &thread_id) ||
                !ParseInteger(fields[5], 10, &creation_time) ||
                !ParseInteger(fields[7], 16, &original_style) ||
                !ParseInteger(fields[8], 16, &applied_style) ||
                hwnd_value == 0 || process_id == 0 || thread_id == 0 ||
                creation_time == 0) {
                result.error_message =
                    ParseError(line_number, L"numeric identity data is invalid.");
                return result;
            }

            state.identity.hwnd = reinterpret_cast<HWND>(hwnd_value);
            state.identity.process_id = process_id;
            state.identity.thread_id = thread_id;
            state.identity.process_creation_time = creation_time;
            state.identity.class_name = Utf8ToWide(fields[6]);
            if (state.identity.class_name.empty() ||
                state.identity.class_name.size() > 255) {
                result.error_message =
                    ParseError(line_number, L"window class is invalid UTF-8.");
                return result;
            }
            state.original_extended_style =
                static_cast<LONG_PTR>(original_style);
            state.applied_extended_style =
                static_cast<LONG_PTR>(applied_style);
            state.modification_applied = true;

            if (!identities.insert(IdentityKey(state)).second) {
                result.error_message =
                    ParseError(line_number, L"duplicate window identity.");
                return result;
            }
            parsed_states.push_back(std::move(state));
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }

    result.succeeded = header_seen;
    if (result.succeeded) {
        result.states = std::move(parsed_states);
    }
    return result;
}

RecoveryLoadResult RecoveryJournal::Load() const {
    RecoveryLoadResult result;
    if (path_.empty()) {
        result.error_message = L"The recovery journal path is empty.";
        return result;
    }

    std::error_code error;
    result.file_found = std::filesystem::exists(path_, error);
    if (error) {
        result.error_message = L"Unable to query the recovery journal.";
        return result;
    }
    if (!result.file_found) {
        result.succeeded = true;
        return result;
    }
    const std::uintmax_t size = std::filesystem::file_size(path_, error);
    if (error || size > kMaximumJournalSize) {
        result.error_message = L"The recovery journal is unavailable or too large.";
        return result;
    }

    std::ifstream input(path_, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        result.error_message = L"Unable to open the recovery journal.";
        return result;
    }
    const std::string serialized{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        result.error_message = L"Reading the recovery journal failed.";
        return result;
    }

    RecoveryParseResult parsed = ParseRecoveryStates(serialized);
    result.succeeded = parsed.succeeded;
    result.states = std::move(parsed.states);
    result.error_message = std::move(parsed.error_message);
    return result;
}

bool RecoveryJournal::Save(
    const std::span<const TaskbarMutationState> states,
    std::wstring* const error_message) {
    if (path_.empty()) {
        if (error_message != nullptr) {
            *error_message = L"The recovery journal path is empty.";
        }
        return false;
    }
    const std::string serialized = SerializeRecoveryStates(states);
    if (serialized.empty()) {
        if (error_message != nullptr) {
            *error_message = L"Recovery state serialization failed.";
        }
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = L"Creating the recovery directory failed (error " +
                             std::to_wstring(error.value()) + L").";
        }
        return false;
    }

    std::filesystem::path temporary = path_;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream output(
            temporary,
            std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error_message != nullptr) {
                *error_message = L"Opening the temporary recovery journal failed.";
            }
            return false;
        }
        output.write(
            serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output.good()) {
            if (error_message != nullptr) {
                *error_message = L"Writing the temporary recovery journal failed.";
            }
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    if (MoveFileExW(
            temporary.c_str(), path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD move_error = GetLastError();
        std::filesystem::remove(temporary, error);
        if (error_message != nullptr) {
            *error_message = L"Replacing the recovery journal failed (Win32 " +
                             std::to_wstring(move_error) + L").";
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

}  // namespace ctm
