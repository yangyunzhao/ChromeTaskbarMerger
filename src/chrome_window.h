#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

enum class WindowExclusionReason {
    ProcessPathUnavailable,
    NotChromeExecutable,
    NotTopLevel,
    NotVisible,
    EmptyTitle,
    ClassNameUnavailable,
    UnsupportedWindowClass,
    StyleUnavailable,
    ExtendedStyleUnavailable,
    ChildWindow,
    ToolWindow,
    NoActivate,
};

struct ChromeWindowSnapshot {
    HWND hwnd = nullptr;
    DWORD process_id = 0;
    DWORD thread_id = 0;
    std::wstring process_path;
    std::wstring title;
    std::wstring class_name;
    HWND owner = nullptr;
    LONG_PTR style = 0;
    LONG_PTR extended_style = 0;
    DWORD process_query_error = ERROR_SUCCESS;
    bool process_path_available = false;
    bool class_name_available = false;
    bool style_available = false;
    bool extended_style_available = false;
    bool top_level = false;
    bool visible = false;
    bool cloaked = false;
    bool cloaked_query_available = false;
};

struct ChromeWindowAssessment {
    bool chrome_process = false;
    bool manageable = false;
    std::vector<WindowExclusionReason> exclusion_reasons;
};

struct ChromeWindowRecord {
    ChromeWindowSnapshot snapshot;
    ChromeWindowAssessment assessment;
};

struct ChromeWindowEnumerationResult {
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
    std::size_t scanned_top_level_windows = 0;
    std::size_t process_query_failures = 0;
    std::vector<ChromeWindowRecord> chrome_windows;
};

[[nodiscard]] ChromeWindowAssessment EvaluateChromeWindow(
    const ChromeWindowSnapshot& snapshot);
[[nodiscard]] bool IsManageableChromeWindow(HWND hwnd);
[[nodiscard]] ChromeWindowEnumerationResult EnumerateChromeWindows();
[[nodiscard]] std::wstring_view WindowExclusionReasonText(
    WindowExclusionReason reason);
[[nodiscard]] std::wstring FormatChromeWindowRecord(
    const ChromeWindowRecord& record,
    std::size_t index);
[[nodiscard]] std::wstring FormatChromeWindowSummary(
    const ChromeWindowEnumerationResult& result);

}  // namespace ctm
