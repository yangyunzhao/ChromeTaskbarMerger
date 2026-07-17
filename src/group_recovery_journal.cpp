#include "group_recovery_journal.h"

#include "window_identity_query.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace ctm {
namespace {

constexpr std::string_view kHeader =
    "ChromeTaskbarMergerGroupRecovery\t1";
constexpr std::uintmax_t kMaximumJournalSize = 1024U * 1024U;
constexpr std::size_t kMaximumMembers = 256;
constexpr std::size_t kMaximumTaskbarStates = 256;

[[nodiscard]] int RectangleWidth(const RECT& rectangle) noexcept {
    return rectangle.right - rectangle.left;
}

[[nodiscard]] int RectangleHeight(const RECT& rectangle) noexcept {
    return rectangle.bottom - rectangle.top;
}

[[nodiscard]] bool RectangleDimensionsAreSafe(
    const RECT& rectangle) noexcept {
    const std::int64_t width =
        static_cast<std::int64_t>(rectangle.right) - rectangle.left;
    const std::int64_t height =
        static_cast<std::int64_t>(rectangle.bottom) - rectangle.top;
    constexpr std::int64_t kMaximumDimension = 100000;
    constexpr std::int64_t kMaximumCoordinateMagnitude = 1000000;
    return width > 0 && height > 0 && width <= kMaximumDimension &&
           height <= kMaximumDimension &&
           rectangle.left >= -kMaximumCoordinateMagnitude &&
           rectangle.top >= -kMaximumCoordinateMagnitude &&
           rectangle.right <= kMaximumCoordinateMagnitude &&
           rectangle.bottom <= kMaximumCoordinateMagnitude;
}

[[nodiscard]] bool RectanglesEqual(const RECT& left,
                                   const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] bool RectangleInside(const RECT& inner,
                                   const RECT& outer) noexcept {
    return RectangleWidth(inner) > 0 && RectangleHeight(inner) > 0 &&
           inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}

[[nodiscard]] bool RectanglesIntersect(const RECT& left,
                                       const RECT& right) noexcept {
    return std::max(left.left, right.left) <
               std::min(left.right, right.right) &&
           std::max(left.top, right.top) <
               std::min(left.bottom, right.bottom);
}

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
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        encoded.data(),
        required,
        nullptr,
        nullptr);
    return written == required ? encoded : std::string{};
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
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        decoded.data(),
        required);
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

[[nodiscard]] bool ParseBoolean(const std::string_view value,
                                bool* const parsed_value) noexcept {
    if (value == "0") {
        *parsed_value = false;
        return true;
    }
    if (value == "1") {
        *parsed_value = true;
        return true;
    }
    return false;
}

[[nodiscard]] std::wstring ParseError(const std::size_t line,
                                      const std::wstring_view detail) {
    return L"Group recovery journal line " + std::to_wstring(line) +
           L": " + std::wstring(detail);
}

[[nodiscard]] std::string IdentityKey(const WindowIdentity& identity) {
    return std::to_string(
               reinterpret_cast<std::uintptr_t>(identity.hwnd)) +
           ":" + std::to_string(identity.process_id) + ":" +
           std::to_string(identity.thread_id) + ":" +
           std::to_string(identity.process_creation_time) + ":" +
           WideToUtf8(identity.class_name);
}

[[nodiscard]] bool TextFieldIsValid(const std::wstring_view value) {
    if (value.empty() || value.size() > 255) {
        return false;
    }
    const std::string encoded = WideToUtf8(value);
    return !encoded.empty() &&
           encoded.find_first_of("\t\r\n") == std::string::npos;
}

[[nodiscard]] bool IdentityIsValid(const WindowIdentity& identity) {
    return WindowIdentityIsComplete(identity) &&
           TextFieldIsValid(identity.class_name);
}

[[nodiscard]] bool MemberIsValid(
    const GroupMemberRecoveryState& member) {
    constexpr UINT kKnownPlacementFlags =
        WPF_SETMINPOSITION | WPF_RESTORETOMAXIMIZED |
        WPF_ASYNCWINDOWPLACEMENT;
    const bool show_command_valid =
        member.original_placement.showCmd >= SW_SHOWNORMAL &&
        member.original_placement.showCmd <= SW_FORCEMINIMIZE;
    return IdentityIsValid(member.identity) &&
           member.original_placement.length == sizeof(WINDOWPLACEMENT) &&
           (member.original_placement.flags & ~kKnownPlacementFlags) == 0 &&
           show_command_valid &&
           RectangleDimensionsAreSafe(
               member.original_placement.rcNormalPosition) &&
           RectangleDimensionsAreSafe(member.original_rectangle) &&
           TextFieldIsValid(member.display.device_name) &&
           RectangleDimensionsAreSafe(member.display.monitor_bounds) &&
           RectangleDimensionsAreSafe(member.display.work_area) &&
           RectangleInside(
               member.display.work_area, member.display.monitor_bounds) &&
           RectanglesIntersect(
               member.original_rectangle, member.display.monitor_bounds) &&
           (!member.layout_restore_completed ||
            member.layout_restore_required);
}

[[nodiscard]] bool StateIsValid(const GroupRecoveryState& state) {
    if (state.members.size() > kMaximumMembers ||
        state.taskbar_states.size() > kMaximumTaskbarStates) {
        return false;
    }
    if (!state.session_active &&
        (state.tab_strip_created || !state.members.empty() ||
         !state.taskbar_states.empty())) {
        return false;
    }

    std::unordered_set<std::string> member_identities;
    for (const GroupMemberRecoveryState& member : state.members) {
        if (!MemberIsValid(member) ||
            !member_identities.insert(IdentityKey(member.identity)).second) {
            return false;
        }
    }

    std::unordered_set<std::string> taskbar_identities;
    for (const TaskbarMutationState& taskbar : state.taskbar_states) {
        if (!taskbar.NeedsRestore() ||
            !IdentityIsValid(taskbar.identity) ||
            (taskbar.method != TaskbarMethod::TaskbarList &&
             taskbar.method != TaskbarMethod::WindowStyle) ||
            !taskbar_identities.insert(IdentityKey(taskbar.identity)).second) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] GroupMemberRecoveryState* FindMember(
    GroupRecoveryState* const state,
    const WindowIdentity& identity) noexcept {
    const auto match = std::find_if(
        state->members.begin(),
        state->members.end(),
        [&identity](const GroupMemberRecoveryState& member) {
            return WindowIdentitiesMatch(member.identity, identity);
        });
    return match == state->members.end() ? nullptr : &*match;
}

[[nodiscard]] bool CaptureMember(
    const WindowIdentity& identity,
    GroupMemberRecoveryState* const member,
    std::wstring* const error_message) {
    const WindowIdentityQueryResult current =
        QueryWindowIdentity(identity.hwnd);
    if (!current.succeeded ||
        !WindowIdentitiesMatch(identity, current.identity)) {
        if (error_message != nullptr) {
            *error_message =
                L"The group recovery identity changed before capture.";
        }
        return false;
    }

    GroupMemberRecoveryState captured;
    captured.identity = identity;
    captured.original_placement.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(
            identity.hwnd, &captured.original_placement) == FALSE ||
        GetWindowRect(identity.hwnd, &captured.original_rectangle) == FALSE) {
        if (error_message != nullptr) {
            *error_message =
                L"Capturing the group recovery window layout failed (Win32 " +
                std::to_wstring(GetLastError()) + L").";
        }
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(
        identity.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor == nullptr ||
        GetMonitorInfoW(monitor, &monitor_info) == FALSE) {
        if (error_message != nullptr) {
            *error_message =
                L"Capturing the group recovery display identity failed (Win32 " +
                std::to_wstring(GetLastError()) + L").";
        }
        return false;
    }
    captured.display.device_name = monitor_info.szDevice;
    captured.display.monitor_bounds = monitor_info.rcMonitor;
    captured.display.work_area = monitor_info.rcWork;
    if (!MemberIsValid(captured)) {
        if (error_message != nullptr) {
            *error_message =
                L"The captured group recovery layout is not trustworthy.";
        }
        return false;
    }
    *member = std::move(captured);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

[[nodiscard]] bool SaveTextAtomically(
    const std::filesystem::path& path,
    const std::string_view serialized,
    std::wstring* const error_message) {
    if (path.empty()) {
        if (error_message != nullptr) {
            *error_message = L"The group recovery journal path is empty.";
        }
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message =
                L"Creating the group recovery directory failed (error " +
                std::to_wstring(error.value()) + L").";
        }
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                 std::to_wstring(GetTickCount64());
    {
        std::ofstream output(
            temporary,
            std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error_message != nullptr) {
                *error_message =
                    L"Opening the temporary group recovery journal failed.";
            }
            return false;
        }
        output.write(
            serialized.data(),
            static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output.good()) {
            if (error_message != nullptr) {
                *error_message =
                    L"Writing the temporary group recovery journal failed.";
            }
            output.close();
            std::filesystem::remove(temporary, error);
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
                L"Replacing the group recovery journal failed (Win32 " +
                std::to_wstring(move_error) + L").";
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

struct DisplayEnumerationContext {
    const GroupDisplayRecoveryState* expected = nullptr;
    bool device_found = false;
    bool exact_match = false;
};

BOOL CALLBACK FindDisplayCallback(const HMONITOR monitor,
                                  HDC,
                                  LPRECT,
                                  const LPARAM parameter) {
    auto* const context =
        reinterpret_cast<DisplayEnumerationContext*>(parameter);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return TRUE;
    }
    if (_wcsicmp(
            info.szDevice,
            context->expected->device_name.c_str()) != 0) {
        return TRUE;
    }
    context->device_found = true;
    context->exact_match = context->exact_match ||
        RectanglesEqual(
            info.rcMonitor, context->expected->monitor_bounds) &&
        RectanglesEqual(info.rcWork, context->expected->work_area);
    return TRUE;
}

[[nodiscard]] GroupRecoveryTargetCheck CheckTarget(
    const GroupMemberRecoveryState& member) {
    const WindowIdentityQueryResult current =
        QueryWindowIdentity(member.identity.hwnd);
    if (!current.window_exists) {
        return {
            .status = GroupRecoveryTargetStatus::Missing,
            .message = L"The original window no longer exists.",
        };
    }
    if (!current.succeeded) {
        return {
            .status = GroupRecoveryTargetStatus::Unavailable,
            .win32_error = current.error_code,
            .message = L"The current window identity could not be queried.",
        };
    }
    if (!WindowIdentitiesMatch(member.identity, current.identity)) {
        return {
            .status = GroupRecoveryTargetStatus::IdentityMismatch,
            .win32_error = ERROR_INVALID_WINDOW_HANDLE,
            .message =
                L"The HWND belongs to a different identity; layout recovery was refused.",
        };
    }

    DisplayEnumerationContext context{.expected = &member.display};
    if (EnumDisplayMonitors(
            nullptr,
            nullptr,
            FindDisplayCallback,
            reinterpret_cast<LPARAM>(&context)) == FALSE) {
        return {
            .status = GroupRecoveryTargetStatus::Unavailable,
            .win32_error = GetLastError(),
            .message = L"Enumerating displays for layout recovery failed.",
        };
    }
    if (!context.device_found || !context.exact_match) {
        return {
            .status = GroupRecoveryTargetStatus::DisplayMismatch,
            .win32_error = ERROR_INVALID_STATE,
            .message =
                L"The original display identity or work area changed; layout recovery was refused.",
        };
    }
    return {
        .status = GroupRecoveryTargetStatus::Valid,
        .message = L"The persisted recovery target is current and safe.",
    };
}

}  // namespace

bool GroupRecoveryState::HasObligations() const noexcept {
    if (session_active || tab_strip_created || !taskbar_states.empty()) {
        return true;
    }
    return std::any_of(
        members.begin(),
        members.end(),
        [](const GroupMemberRecoveryState& member) {
            return member.tab_created || member.NeedsLayoutRestore();
        });
}

std::string SerializeGroupRecoveryState(const GroupRecoveryState& state) {
    if (!StateIsValid(state)) {
        return {};
    }
    std::ostringstream output;
    output << kHeader << '\n'
           << "session\t" << (state.session_active ? 1 : 0) << '\t'
           << (state.tab_strip_created ? 1 : 0) << '\n';
    for (const GroupMemberRecoveryState& member : state.members) {
        output << "member\t" << std::hex << std::uppercase
               << reinterpret_cast<std::uintptr_t>(member.identity.hwnd)
               << std::dec << '\t' << member.identity.process_id << '\t'
               << member.identity.thread_id << '\t'
               << member.identity.process_creation_time << '\t'
               << WideToUtf8(member.identity.class_name) << '\t'
               << member.original_placement.flags << '\t'
               << member.original_placement.showCmd << '\t'
               << member.original_placement.ptMinPosition.x << '\t'
               << member.original_placement.ptMinPosition.y << '\t'
               << member.original_placement.ptMaxPosition.x << '\t'
               << member.original_placement.ptMaxPosition.y << '\t'
               << member.original_placement.rcNormalPosition.left << '\t'
               << member.original_placement.rcNormalPosition.top << '\t'
               << member.original_placement.rcNormalPosition.right << '\t'
               << member.original_placement.rcNormalPosition.bottom << '\t'
               << member.original_rectangle.left << '\t'
               << member.original_rectangle.top << '\t'
               << member.original_rectangle.right << '\t'
               << member.original_rectangle.bottom << '\t'
               << WideToUtf8(member.display.device_name) << '\t'
               << member.display.monitor_bounds.left << '\t'
               << member.display.monitor_bounds.top << '\t'
               << member.display.monitor_bounds.right << '\t'
               << member.display.monitor_bounds.bottom << '\t'
               << member.display.work_area.left << '\t'
               << member.display.work_area.top << '\t'
               << member.display.work_area.right << '\t'
               << member.display.work_area.bottom << '\t'
               << (member.tab_created ? 1 : 0) << '\t'
               << (member.layout_restore_required ? 1 : 0) << '\t'
               << (member.layout_restore_completed ? 1 : 0) << '\n';
    }
    for (const TaskbarMutationState& taskbar : state.taskbar_states) {
        output << "taskbar\t"
               << (taskbar.method == TaskbarMethod::TaskbarList
                       ? "taskbar_list"
                       : "window_style")
               << '\t' << std::hex << std::uppercase
               << reinterpret_cast<std::uintptr_t>(taskbar.identity.hwnd)
               << std::dec << '\t' << taskbar.identity.process_id << '\t'
               << taskbar.identity.thread_id << '\t'
               << taskbar.identity.process_creation_time << '\t'
               << WideToUtf8(taskbar.identity.class_name) << '\t'
               << std::hex << std::uppercase
               << static_cast<std::uintptr_t>(
                      taskbar.original_extended_style)
               << '\t'
               << static_cast<std::uintptr_t>(
                      taskbar.applied_extended_style)
               << std::dec << '\n';
    }
    return output.str();
}

GroupRecoveryParseResult ParseGroupRecoveryState(
    const std::string_view serialized) {
    GroupRecoveryParseResult result;
    if (serialized.empty() || serialized.size() > kMaximumJournalSize) {
        result.error_message =
            L"The group recovery journal is empty or too large.";
        return result;
    }

    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= serialized.size()) {
        const std::size_t newline = serialized.find('\n', start);
        std::string_view line = newline == std::string_view::npos
                                    ? serialized.substr(start)
                                    : serialized.substr(
                                          start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    if (lines.size() < 2 || lines[0] != kHeader) {
        result.error_message =
            L"The group recovery journal header or version is invalid.";
        return result;
    }

    const std::vector session = SplitFields(lines[1]);
    if (session.size() != 3 || session[0] != "session" ||
        !ParseBoolean(session[1], &result.state.session_active) ||
        !ParseBoolean(session[2], &result.state.tab_strip_created)) {
        result.error_message =
            ParseError(2, L"session record is invalid.");
        return result;
    }

    for (std::size_t index = 2; index < lines.size(); ++index) {
        const std::size_t line_number = index + 1;
        const std::vector fields = SplitFields(lines[index]);
        if (!fields.empty() && fields[0] == "member") {
            if (fields.size() != 32 ||
                result.state.members.size() >= kMaximumMembers) {
                result.error_message = ParseError(
                    line_number, L"member record shape or count is invalid.");
                return result;
            }
            GroupMemberRecoveryState member;
            std::uintptr_t hwnd = 0;
            std::uint32_t process_id = 0;
            std::uint32_t thread_id = 0;
            std::uint64_t creation_time = 0;
            std::uint32_t flags = 0;
            std::uint32_t show_command = 0;
            std::int32_t values[20]{};
            bool numeric_valid =
                ParseInteger(fields[1], 16, &hwnd) &&
                ParseInteger(fields[2], 10, &process_id) &&
                ParseInteger(fields[3], 10, &thread_id) &&
                ParseInteger(fields[4], 10, &creation_time) &&
                ParseInteger(fields[6], 10, &flags) &&
                ParseInteger(fields[7], 10, &show_command);
            for (std::size_t value = 0; value < 12; ++value) {
                numeric_valid = numeric_valid && ParseInteger(
                    fields[8 + value], 10, &values[value]);
            }
            for (std::size_t value = 12; value < 20; ++value) {
                numeric_valid = numeric_valid && ParseInteger(
                    fields[9 + value], 10, &values[value]);
            }
            if (!numeric_valid || hwnd == 0 || process_id == 0 ||
                thread_id == 0 || creation_time == 0 ||
                !ParseBoolean(fields[29], &member.tab_created) ||
                !ParseBoolean(
                    fields[30], &member.layout_restore_required) ||
                !ParseBoolean(
                    fields[31], &member.layout_restore_completed)) {
                result.error_message =
                    ParseError(line_number, L"member data is invalid.");
                return result;
            }
            member.identity = {
                .hwnd = reinterpret_cast<HWND>(hwnd),
                .process_id = process_id,
                .thread_id = thread_id,
                .process_creation_time = creation_time,
                .class_name = Utf8ToWide(fields[5]),
            };
            member.original_placement.length = sizeof(WINDOWPLACEMENT);
            member.original_placement.flags = flags;
            member.original_placement.showCmd = show_command;
            member.original_placement.ptMinPosition = {values[0], values[1]};
            member.original_placement.ptMaxPosition = {values[2], values[3]};
            member.original_placement.rcNormalPosition = {
                values[4], values[5], values[6], values[7]};
            member.original_rectangle = {
                values[8], values[9], values[10], values[11]};
            member.display.device_name = Utf8ToWide(fields[20]);
            member.display.monitor_bounds = {
                values[12], values[13], values[14], values[15]};
            member.display.work_area = {
                values[16], values[17], values[18], values[19]};
            result.state.members.push_back(std::move(member));
        } else if (!fields.empty() && fields[0] == "taskbar") {
            if (fields.size() != 9 ||
                result.state.taskbar_states.size() >=
                    kMaximumTaskbarStates) {
                result.error_message = ParseError(
                    line_number, L"taskbar record shape or count is invalid.");
                return result;
            }
            TaskbarMutationState taskbar;
            if (fields[1] == "taskbar_list") {
                taskbar.method = TaskbarMethod::TaskbarList;
            } else if (fields[1] == "window_style") {
                taskbar.method = TaskbarMethod::WindowStyle;
            } else {
                result.error_message = ParseError(
                    line_number, L"taskbar method is invalid.");
                return result;
            }
            std::uintptr_t hwnd = 0;
            std::uint32_t process_id = 0;
            std::uint32_t thread_id = 0;
            std::uint64_t creation_time = 0;
            std::uintptr_t original_style = 0;
            std::uintptr_t applied_style = 0;
            if (!ParseInteger(fields[2], 16, &hwnd) ||
                !ParseInteger(fields[3], 10, &process_id) ||
                !ParseInteger(fields[4], 10, &thread_id) ||
                !ParseInteger(fields[5], 10, &creation_time) ||
                !ParseInteger(fields[7], 16, &original_style) ||
                !ParseInteger(fields[8], 16, &applied_style) ||
                hwnd == 0 || process_id == 0 || thread_id == 0 ||
                creation_time == 0) {
                result.error_message = ParseError(
                    line_number, L"taskbar data is invalid.");
                return result;
            }
            taskbar.identity = {
                .hwnd = reinterpret_cast<HWND>(hwnd),
                .process_id = process_id,
                .thread_id = thread_id,
                .process_creation_time = creation_time,
                .class_name = Utf8ToWide(fields[6]),
            };
            taskbar.original_extended_style =
                static_cast<LONG_PTR>(original_style);
            taskbar.applied_extended_style =
                static_cast<LONG_PTR>(applied_style);
            taskbar.modification_applied = true;
            result.state.taskbar_states.push_back(std::move(taskbar));
        } else {
            result.error_message =
                ParseError(line_number, L"record type is invalid.");
            return result;
        }
    }

    if (!StateIsValid(result.state)) {
        result.state = {};
        result.error_message =
            L"The group recovery journal contains inconsistent or duplicate state.";
        return result;
    }
    result.succeeded = true;
    return result;
}

GroupRecoveryLoadResult GroupRecoveryJournal::Load() const {
    GroupRecoveryLoadResult result;
    if (path_.empty()) {
        result.error_message = L"The group recovery journal path is empty.";
        return result;
    }
    std::error_code error;
    result.file_found = std::filesystem::exists(path_, error);
    if (error) {
        result.error_message =
            L"Unable to query the group recovery journal.";
        return result;
    }
    if (!result.file_found) {
        result.succeeded = true;
        return result;
    }
    const std::uintmax_t size = std::filesystem::file_size(path_, error);
    if (error || size > kMaximumJournalSize) {
        result.error_message =
            L"The group recovery journal is unavailable or too large.";
        return result;
    }
    std::ifstream input(path_, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        result.error_message =
            L"Unable to open the group recovery journal.";
        return result;
    }
    const std::string serialized{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        result.error_message =
            L"Reading the group recovery journal failed.";
        return result;
    }
    GroupRecoveryParseResult parsed =
        ParseGroupRecoveryState(serialized);
    result.succeeded = parsed.succeeded;
    result.state = std::move(parsed.state);
    result.error_message = std::move(parsed.error_message);
    return result;
}

bool GroupRecoveryJournal::Adopt(
    GroupRecoveryState state,
    std::wstring* const error_message) {
    if (!StateIsValid(state)) {
        if (error_message != nullptr) {
            *error_message =
                L"The supplied group recovery state is invalid.";
        }
        return false;
    }
    state_ = std::move(state);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool GroupRecoveryJournal::BeginSession(
    const std::span<const WindowIdentity> identities,
    std::wstring* const error_message) {
    if (state_.HasObligations() || identities.empty()) {
        if (error_message != nullptr) {
            *error_message = state_.HasObligations()
                                 ? L"An older group recovery obligation exists."
                                 : L"A group session requires at least one member.";
        }
        return false;
    }
    GroupRecoveryState candidate;
    candidate.session_active = true;
    candidate.members.reserve(identities.size());
    for (const WindowIdentity& identity : identities) {
        GroupMemberRecoveryState member;
        if (!CaptureMember(identity, &member, error_message)) {
            return false;
        }
        candidate.members.push_back(std::move(member));
    }
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::EnsureMembers(
    const std::span<const WindowIdentity> identities,
    std::wstring* const error_message) {
    if (!state_.session_active) {
        if (error_message != nullptr) {
            *error_message = L"No active group recovery session exists.";
        }
        return false;
    }
    GroupRecoveryState candidate = state_;
    bool changed = false;
    for (const WindowIdentity& identity : identities) {
        if (FindMember(&candidate, identity) != nullptr) {
            continue;
        }
        GroupMemberRecoveryState member;
        if (!CaptureMember(identity, &member, error_message)) {
            return false;
        }
        candidate.members.push_back(std::move(member));
        changed = true;
    }
    if (!changed) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::MarkTabStripCreated(
    const bool created,
    std::wstring* const error_message) {
    GroupRecoveryState candidate = state_;
    candidate.tab_strip_created = created;
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::MarkTabsCreated(
    const std::span<const WindowIdentity> identities,
    const bool created,
    std::wstring* const error_message) {
    GroupRecoveryState candidate = state_;
    for (const WindowIdentity& identity : identities) {
        GroupMemberRecoveryState* const member =
            FindMember(&candidate, identity);
        if (member == nullptr) {
            if (error_message != nullptr) {
                *error_message =
                    L"A tab recovery member is not in the persisted group.";
            }
            return false;
        }
        member->tab_created = created;
    }
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::PlanLayoutMutation(
    const std::span<const WindowIdentity> identities,
    std::wstring* const error_message) {
    GroupRecoveryState candidate = state_;
    bool changed = false;
    for (const WindowIdentity& identity : identities) {
        GroupMemberRecoveryState* const member =
            FindMember(&candidate, identity);
        if (member == nullptr) {
            if (error_message != nullptr) {
                *error_message =
                    L"A layout recovery member is not in the persisted group.";
            }
            return false;
        }
        if (!member->layout_restore_required ||
            member->layout_restore_completed) {
            member->layout_restore_required = true;
            member->layout_restore_completed = false;
            changed = true;
        }
    }
    if (!changed) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::MarkLayoutRestored(
    const WindowIdentity& identity,
    std::wstring* const error_message) {
    GroupRecoveryState candidate = state_;
    GroupMemberRecoveryState* const member = FindMember(&candidate, identity);
    if (member == nullptr || !member->layout_restore_required) {
        if (error_message != nullptr) {
            *error_message =
                L"The completed layout is not a pending persisted member.";
        }
        return false;
    }
    member->layout_restore_completed = true;
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::Clear(std::wstring* const error_message) {
    return PersistCandidate({}, error_message);
}

bool GroupRecoveryJournal::Save(
    const std::span<const TaskbarMutationState> states,
    std::wstring* const error_message) {
    GroupRecoveryState candidate = state_;
    candidate.taskbar_states.assign(states.begin(), states.end());
    return PersistCandidate(std::move(candidate), error_message);
}

bool GroupRecoveryJournal::PersistCandidate(
    GroupRecoveryState candidate,
    std::wstring* const error_message) {
    const std::string serialized = SerializeGroupRecoveryState(candidate);
    if (serialized.empty()) {
        if (error_message != nullptr) {
            *error_message =
                L"Group recovery state serialization failed.";
        }
        return false;
    }
    const bool saved = persistence_ != nullptr
                           ? persistence_->SaveAtomically(
                                 path_, serialized, error_message)
                           : SaveTextAtomically(
                                 path_, serialized, error_message);
    if (!saved) {
        return false;
    }
    state_ = std::move(candidate);
    return true;
}

GroupRecoveryTargetCheck Win32GroupRecoveryWindowGateway::Check(
    const GroupMemberRecoveryState& member) {
    return CheckTarget(member);
}

GroupRecoveryTargetCheck Win32GroupRecoveryWindowGateway::Restore(
    const GroupMemberRecoveryState& member) {
    const GroupRecoveryTargetCheck validation = CheckTarget(member);
    if (validation.status != GroupRecoveryTargetStatus::Valid) {
        return validation;
    }
    WINDOWPLACEMENT placement = member.original_placement;
    placement.length = sizeof(placement);
    if (SetWindowPlacement(member.identity.hwnd, &placement) == FALSE) {
        return {
            .status = GroupRecoveryTargetStatus::Unavailable,
            .win32_error = GetLastError(),
            .message = L"SetWindowPlacement failed during persisted recovery.",
        };
    }
    if (SetWindowPos(
            member.identity.hwnd,
            nullptr,
            member.original_rectangle.left,
            member.original_rectangle.top,
            RectangleWidth(member.original_rectangle),
            RectangleHeight(member.original_rectangle),
            SWP_NOZORDER | SWP_NOACTIVATE) == FALSE) {
        return {
            .status = GroupRecoveryTargetStatus::Unavailable,
            .win32_error = GetLastError(),
            .message = L"SetWindowPos failed during persisted recovery.",
        };
    }
    RECT restored{};
    if (GetWindowRect(member.identity.hwnd, &restored) == FALSE ||
        !RectanglesEqual(restored, member.original_rectangle)) {
        return {
            .status = GroupRecoveryTargetStatus::Unavailable,
            .win32_error = GetLastError(),
            .message =
                L"The persisted recovery rectangle could not be verified.",
        };
    }
    return {
        .status = GroupRecoveryTargetStatus::Valid,
        .message = L"The original persisted window layout was restored.",
    };
}

GroupLayoutRecoveryReport RestorePersistedGroupLayouts(
    GroupRecoveryJournal* const journal,
    IGroupRecoveryWindowGateway* const gateway) {
    GroupLayoutRecoveryReport report;
    if (journal == nullptr || gateway == nullptr) {
        report.succeeded = false;
        report.persistence_error =
            L"The group layout recovery service is unavailable.";
        return report;
    }

    std::vector<GroupMemberRecoveryState> pending;
    for (const GroupMemberRecoveryState& member : journal->state().members) {
        if (member.NeedsLayoutRestore()) {
            pending.push_back(member);
        }
    }
    for (const GroupMemberRecoveryState& member : pending) {
        const GroupRecoveryTargetCheck checked = gateway->Check(member);
        GroupLayoutRecoveryOperation operation;
        operation.identity = member.identity;
        operation.status = checked.status;
        operation.win32_error = checked.win32_error;
        operation.message = checked.message;

        if (checked.status == GroupRecoveryTargetStatus::Missing ||
            checked.status == GroupRecoveryTargetStatus::IdentityMismatch) {
            operation.succeeded = true;
            operation.safely_skipped = true;
            ++report.safely_skipped_count;
        } else if (checked.status == GroupRecoveryTargetStatus::Valid) {
            const GroupRecoveryTargetCheck restored =
                gateway->Restore(member);
            operation.status = restored.status;
            operation.win32_error = restored.win32_error;
            operation.message = restored.message;
            operation.succeeded =
                restored.status == GroupRecoveryTargetStatus::Valid;
            if (operation.succeeded) {
                ++report.restored_count;
            }
        }

        if (!operation.succeeded) {
            report.succeeded = false;
            report.operations.push_back(std::move(operation));
            continue;
        }

        std::wstring persistence_error;
        if (!journal->MarkLayoutRestored(
                member.identity, &persistence_error)) {
            operation.succeeded = false;
            operation.message +=
                L" Persisting layout completion failed: " +
                persistence_error;
            report.succeeded = false;
            report.persistence_error = std::move(persistence_error);
        }
        report.operations.push_back(std::move(operation));
    }
    return report;
}

}  // namespace ctm
