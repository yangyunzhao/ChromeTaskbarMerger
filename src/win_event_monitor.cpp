#include "win_event_monitor.h"

#include <atomic>

namespace ctm {
namespace {

std::atomic<ChromeWinEventMonitor*> g_active_monitor = nullptr;

void AssignError(DWORD* const error_code, const DWORD value) noexcept {
    if (error_code != nullptr) {
        *error_code = value;
    }
}

}  // namespace

std::optional<ChromeWindowEventKind> TranslateWinEvent(
    const DWORD event,
    const LONG object_id,
    const LONG child_id) noexcept {
    switch (event) {
        case EVENT_SYSTEM_FOREGROUND:
            return ChromeWindowEventKind::Foreground;
        case EVENT_SYSTEM_MINIMIZESTART:
            return ChromeWindowEventKind::MinimizeStarted;
        case EVENT_SYSTEM_MINIMIZEEND:
            return ChromeWindowEventKind::MinimizeEnded;
        case EVENT_SYSTEM_MOVESIZEEND:
            return ChromeWindowEventKind::MoveSizeEnded;
        default:
            break;
    }
    if (object_id != OBJID_WINDOW || child_id != CHILDID_SELF) {
        return std::nullopt;
    }
    switch (event) {
        case EVENT_OBJECT_CREATE:
            return ChromeWindowEventKind::Created;
        case EVENT_OBJECT_DESTROY:
            return ChromeWindowEventKind::Destroyed;
        case EVENT_OBJECT_SHOW:
            return ChromeWindowEventKind::Shown;
        case EVENT_OBJECT_HIDE:
            return ChromeWindowEventKind::Hidden;
        case EVENT_OBJECT_NAMECHANGE:
            return ChromeWindowEventKind::NameChanged;
        case EVENT_OBJECT_LOCATIONCHANGE:
            return ChromeWindowEventKind::LocationChanged;
        default:
            return std::nullopt;
    }
}

ChromeWinEventMonitor::~ChromeWinEventMonitor() {
    Stop();
}

bool ChromeWinEventMonitor::Start(
    const DWORD target_thread_id,
    const UINT target_message,
    const bool skip_own_process,
    DWORD* const error_code) noexcept {
    if (hook_ != nullptr || target_thread_id == 0 ||
        target_message < WM_APP) {
        AssignError(error_code, ERROR_INVALID_PARAMETER);
        return false;
    }

    ChromeWinEventMonitor* expected = nullptr;
    if (!g_active_monitor.compare_exchange_strong(expected, this)) {
        AssignError(error_code, ERROR_ALREADY_EXISTS);
        return false;
    }
    target_thread_id_ = target_thread_id;
    target_message_ = target_message;
    DWORD flags = WINEVENT_OUTOFCONTEXT;
    if (skip_own_process) {
        flags |= WINEVENT_SKIPOWNPROCESS;
    }
    hook_ = SetWinEventHook(
        EVENT_MIN,
        EVENT_MAX,
        nullptr,
        &ChromeWinEventMonitor::WinEventCallback,
        0,
        0,
        flags);
    if (hook_ == nullptr) {
        const DWORD hook_error = GetLastError();
        target_thread_id_ = 0;
        target_message_ = 0;
        expected = this;
        static_cast<void>(
            g_active_monitor.compare_exchange_strong(expected, nullptr));
        AssignError(
            error_code,
            hook_error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : hook_error);
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

void ChromeWinEventMonitor::Stop() noexcept {
    if (hook_ != nullptr) {
        UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
    ChromeWinEventMonitor* expected = this;
    static_cast<void>(
        g_active_monitor.compare_exchange_strong(expected, nullptr));
    target_thread_id_ = 0;
    target_message_ = 0;
}

ChromeWindowEvent ChromeWinEventMonitor::DecodeMessage(
    const WPARAM wparam,
    const LPARAM lparam) noexcept {
    return {
        .kind = static_cast<ChromeWindowEventKind>(lparam),
        .hwnd = reinterpret_cast<HWND>(wparam),
    };
}

void CALLBACK ChromeWinEventMonitor::WinEventCallback(
    HWINEVENTHOOK,
    const DWORD event,
    const HWND hwnd,
    const LONG object_id,
    const LONG child_id,
    DWORD,
    DWORD) noexcept {
    const std::optional<ChromeWindowEventKind> translated =
        TranslateWinEvent(event, object_id, child_id);
    if (!translated.has_value() || hwnd == nullptr) {
        return;
    }
    ChromeWinEventMonitor* const monitor = g_active_monitor.load();
    if (monitor != nullptr) {
        static_cast<void>(monitor->Post(*translated, hwnd));
    }
}

bool ChromeWinEventMonitor::Post(
    const ChromeWindowEventKind kind,
    const HWND hwnd) const noexcept {
    return target_thread_id_ != 0 && target_message_ >= WM_APP &&
           PostThreadMessageW(
               target_thread_id_,
               target_message_,
               reinterpret_cast<WPARAM>(hwnd),
               static_cast<LPARAM>(kind)) != FALSE;
}

}  // namespace ctm
