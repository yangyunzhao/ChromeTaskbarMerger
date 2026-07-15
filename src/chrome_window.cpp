#include "chrome_window.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace ctm {
namespace {

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueHandle() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

struct EnumerationContext {
    ChromeWindowEnumerationResult result;
    bool callback_failed = false;
};

[[nodiscard]] bool EqualsCaseInsensitive(const std::wstring_view left,
                                         const std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    return CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool IsChromeExecutablePath(const std::wstring_view path) {
    if (path.empty()) {
        return false;
    }

    const std::wstring filename =
        std::filesystem::path(path).filename().wstring();
    return EqualsCaseInsensitive(filename, L"chrome.exe");
}

[[nodiscard]] bool IsSupportedChromeClass(const std::wstring_view class_name) {
    constexpr std::wstring_view prefix = L"Chrome_WidgetWin_";
    return class_name.size() > prefix.size() && class_name.starts_with(prefix);
}

[[nodiscard]] std::wstring QueryProcessPath(const DWORD process_id,
                                            DWORD* const error_code) {
    const UniqueHandle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (process.get() == nullptr) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        return {};
    }

    std::vector<wchar_t> buffer(32'768);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size) ==
        FALSE) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        return {};
    }

    if (error_code != nullptr) {
        *error_code = ERROR_SUCCESS;
    }
    return std::wstring(buffer.data(), size);
}

[[nodiscard]] std::wstring QueryWindowTitle(const HWND hwnd) {
    const int title_length = GetWindowTextLengthW(hwnd);
    if (title_length <= 0) {
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(title_length) + 1);
    const int copied =
        GetWindowTextW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {};
    }

    return std::wstring(buffer.data(), static_cast<std::size_t>(copied));
}

[[nodiscard]] std::pair<std::wstring, bool> QueryWindowClass(
    const HWND hwnd) {
    std::vector<wchar_t> buffer(256);
    const int copied =
        GetClassNameW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {{}, false};
    }

    return {
        std::wstring(buffer.data(), static_cast<std::size_t>(copied)),
        true,
    };
}

[[nodiscard]] std::pair<LONG_PTR, bool> QueryWindowLongPtr(
    const HWND hwnd,
    const int index) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR value = GetWindowLongPtrW(hwnd, index);
    const DWORD error = GetLastError();
    return {value, value != 0 || error == ERROR_SUCCESS};
}

[[nodiscard]] ChromeWindowSnapshot InspectWindow(const HWND hwnd) {
    ChromeWindowSnapshot snapshot;
    snapshot.hwnd = hwnd;
    snapshot.thread_id =
        GetWindowThreadProcessId(hwnd, &snapshot.process_id);
    snapshot.process_path =
        QueryProcessPath(snapshot.process_id, &snapshot.process_query_error);
    snapshot.process_path_available = !snapshot.process_path.empty();
    snapshot.title = QueryWindowTitle(hwnd);

    auto [class_name, class_available] = QueryWindowClass(hwnd);
    snapshot.class_name = std::move(class_name);
    snapshot.class_name_available = class_available;

    auto [style, style_available] = QueryWindowLongPtr(hwnd, GWL_STYLE);
    snapshot.style = style;
    snapshot.style_available = style_available;

    auto [extended_style, extended_style_available] =
        QueryWindowLongPtr(hwnd, GWL_EXSTYLE);
    snapshot.extended_style = extended_style;
    snapshot.extended_style_available = extended_style_available;

    snapshot.owner = GetWindow(hwnd, GW_OWNER);
    snapshot.top_level = GetAncestor(hwnd, GA_ROOT) == hwnd;
    snapshot.visible = IsWindowVisible(hwnd) != FALSE;

    DWORD cloaked = 0;
    const HRESULT cloak_result = DwmGetWindowAttribute(
        hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    snapshot.cloaked_query_available = SUCCEEDED(cloak_result);
    snapshot.cloaked = snapshot.cloaked_query_available && cloaked != 0;
    return snapshot;
}

[[nodiscard]] std::wstring SanitizeSingleLine(std::wstring value) {
    std::replace_if(
        value.begin(),
        value.end(),
        [](const wchar_t character) {
            return character == L'\r' || character == L'\n' ||
                   character == L'\t';
        },
        L' ');
    return value;
}

[[nodiscard]] std::wstring FormatHandle(const HWND hwnd) {
    std::wostringstream output;
    output << L"0x" << std::uppercase << std::hex << std::setfill(L'0')
           << std::setw(static_cast<int>(sizeof(std::uintptr_t) * 2))
           << reinterpret_cast<std::uintptr_t>(hwnd);
    return output.str();
}

[[nodiscard]] std::wstring FormatStyle(const LONG_PTR style) {
    std::wostringstream output;
    output << L"0x" << std::uppercase << std::hex << std::setfill(L'0')
           << std::setw(static_cast<int>(sizeof(LONG_PTR) * 2))
           << static_cast<std::uintptr_t>(style);
    return output.str();
}

BOOL CALLBACK EnumerateWindowCallback(const HWND hwnd,
                                      const LPARAM parameter) noexcept {
    auto* const context =
        reinterpret_cast<EnumerationContext*>(parameter);
    try {
        ++context->result.scanned_top_level_windows;
        ChromeWindowSnapshot snapshot = InspectWindow(hwnd);
        if (!snapshot.process_path_available) {
            ++context->result.process_query_failures;
        }

        ChromeWindowAssessment assessment = EvaluateChromeWindow(snapshot);
        if (assessment.chrome_process) {
            context->result.chrome_windows.push_back({
                .snapshot = std::move(snapshot),
                .assessment = std::move(assessment),
            });
        }
        return TRUE;
    } catch (...) {
        context->callback_failed = true;
        return FALSE;
    }
}

}  // namespace

ChromeWindowAssessment EvaluateChromeWindow(
    const ChromeWindowSnapshot& snapshot) {
    ChromeWindowAssessment assessment;
    auto exclude = [&assessment](const WindowExclusionReason reason) {
        assessment.exclusion_reasons.push_back(reason);
    };

    if (!snapshot.process_path_available) {
        exclude(WindowExclusionReason::ProcessPathUnavailable);
        return assessment;
    }

    if (!IsChromeExecutablePath(snapshot.process_path)) {
        exclude(WindowExclusionReason::NotChromeExecutable);
        return assessment;
    }

    assessment.chrome_process = true;
    if (!snapshot.top_level) {
        exclude(WindowExclusionReason::NotTopLevel);
    }
    if (!snapshot.visible) {
        exclude(WindowExclusionReason::NotVisible);
    }
    if (snapshot.title.empty()) {
        exclude(WindowExclusionReason::EmptyTitle);
    }
    if (!snapshot.class_name_available) {
        exclude(WindowExclusionReason::ClassNameUnavailable);
    } else if (!IsSupportedChromeClass(snapshot.class_name)) {
        exclude(WindowExclusionReason::UnsupportedWindowClass);
    }

    if (!snapshot.style_available) {
        exclude(WindowExclusionReason::StyleUnavailable);
    } else if ((snapshot.style & WS_CHILD) != 0) {
        exclude(WindowExclusionReason::ChildWindow);
    }

    if (!snapshot.extended_style_available) {
        exclude(WindowExclusionReason::ExtendedStyleUnavailable);
    } else {
        if ((snapshot.extended_style & WS_EX_TOOLWINDOW) != 0) {
            exclude(WindowExclusionReason::ToolWindow);
        }
        if ((snapshot.extended_style & WS_EX_NOACTIVATE) != 0) {
            exclude(WindowExclusionReason::NoActivate);
        }
    }

    assessment.manageable = assessment.exclusion_reasons.empty();
    return assessment;
}

bool IsManageableChromeWindow(const HWND hwnd) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return false;
    }

    try {
        return EvaluateChromeWindow(InspectWindow(hwnd)).manageable;
    } catch (...) {
        return false;
    }
}

ChromeWindowEnumerationResult EnumerateChromeWindows() {
    EnumerationContext context;
    SetLastError(ERROR_SUCCESS);
    const BOOL enumerated = EnumWindows(
        EnumerateWindowCallback,
        reinterpret_cast<LPARAM>(&context));
    if (enumerated == FALSE) {
        context.result.succeeded = false;
        context.result.error_code = context.callback_failed
                                        ? ERROR_OUTOFMEMORY
                                        : GetLastError();
        if (context.result.error_code == ERROR_SUCCESS) {
            context.result.error_code = ERROR_GEN_FAILURE;
        }
        return context.result;
    }

    context.result.succeeded = true;
    return context.result;
}

std::wstring_view WindowExclusionReasonText(
    const WindowExclusionReason reason) {
    switch (reason) {
        case WindowExclusionReason::ProcessPathUnavailable:
            return L"process path unavailable";
        case WindowExclusionReason::NotChromeExecutable:
            return L"process executable is not chrome.exe";
        case WindowExclusionReason::NotTopLevel:
            return L"window is not top-level";
        case WindowExclusionReason::NotVisible:
            return L"window is not visible";
        case WindowExclusionReason::EmptyTitle:
            return L"window title is empty";
        case WindowExclusionReason::ClassNameUnavailable:
            return L"window class is unavailable";
        case WindowExclusionReason::UnsupportedWindowClass:
            return L"window class is not Chrome_WidgetWin_*";
        case WindowExclusionReason::StyleUnavailable:
            return L"window style is unavailable";
        case WindowExclusionReason::ExtendedStyleUnavailable:
            return L"extended window style is unavailable";
        case WindowExclusionReason::ChildWindow:
            return L"window has WS_CHILD";
        case WindowExclusionReason::ToolWindow:
            return L"window has WS_EX_TOOLWINDOW";
        case WindowExclusionReason::NoActivate:
            return L"window has WS_EX_NOACTIVATE";
    }

    return L"unknown exclusion reason";
}

std::wstring FormatChromeWindowRecord(const ChromeWindowRecord& record,
                                      const std::size_t index) {
    const ChromeWindowSnapshot& snapshot = record.snapshot;
    std::wostringstream output;
    output << L'[' << index << L"] "
           << (record.assessment.manageable ? L"MANAGEABLE" : L"EXCLUDED")
           << L'\n'
           << L"  HWND: " << FormatHandle(snapshot.hwnd) << L'\n'
           << L"  PID/TID: " << snapshot.process_id << L'/'
           << snapshot.thread_id << L'\n'
           << L"  Process: " << SanitizeSingleLine(snapshot.process_path)
           << L'\n'
           << L"  Title: " << SanitizeSingleLine(snapshot.title) << L'\n'
           << L"  Class: " << SanitizeSingleLine(snapshot.class_name) << L'\n'
           << L"  Visible: " << (snapshot.visible ? L"yes" : L"no")
           << L"; Cloaked: "
           << (snapshot.cloaked_query_available
                   ? (snapshot.cloaked ? L"yes" : L"no")
                   : L"unknown")
           << L"; Top-level: " << (snapshot.top_level ? L"yes" : L"no")
           << L'\n'
           << L"  Owner: " << FormatHandle(snapshot.owner) << L'\n'
           << L"  Style: "
           << (snapshot.style_available ? FormatStyle(snapshot.style)
                                        : L"unavailable")
           << L'\n'
           << L"  ExStyle: "
           << (snapshot.extended_style_available
                   ? FormatStyle(snapshot.extended_style)
                   : L"unavailable")
           << L'\n'
           << L"  Decision: ";

    if (record.assessment.manageable) {
        output << L"manageable Chrome main-window candidate";
    } else {
        for (std::size_t reason_index = 0;
             reason_index < record.assessment.exclusion_reasons.size();
             ++reason_index) {
            if (reason_index != 0) {
                output << L"; ";
            }
            output << WindowExclusionReasonText(
                record.assessment.exclusion_reasons[reason_index]);
        }
    }
    return output.str();
}

std::wstring FormatChromeWindowSummary(
    const ChromeWindowEnumerationResult& result) {
    const std::size_t manageable_count = static_cast<std::size_t>(
        std::count_if(
            result.chrome_windows.begin(),
            result.chrome_windows.end(),
            [](const ChromeWindowRecord& record) {
                return record.assessment.manageable;
            }));
    const std::size_t excluded_count =
        result.chrome_windows.size() - manageable_count;

    std::wostringstream output;
    output << L"Scanned top-level windows: "
           << result.scanned_top_level_windows << L'\n'
           << L"Process-query failures: " << result.process_query_failures
           << L'\n'
           << L"Chrome candidates: " << result.chrome_windows.size() << L'\n'
           << L"Manageable: " << manageable_count
           << L"; Excluded: " << excluded_count;
    return output.str();
}

}  // namespace ctm
