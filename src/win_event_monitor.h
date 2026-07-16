#pragma once

#include "lifecycle_sync.h"

#include <Windows.h>

#include <optional>

namespace ctm {

[[nodiscard]] std::optional<ChromeWindowEventKind> TranslateWinEvent(
    DWORD event,
    LONG object_id,
    LONG child_id) noexcept;

class ChromeWinEventMonitor final {
public:
    ChromeWinEventMonitor() = default;
    ~ChromeWinEventMonitor();

    ChromeWinEventMonitor(const ChromeWinEventMonitor&) = delete;
    ChromeWinEventMonitor& operator=(const ChromeWinEventMonitor&) = delete;

    [[nodiscard]] bool Start(
        DWORD target_thread_id,
        UINT target_message,
        bool skip_own_process,
        DWORD* error_code) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool active() const noexcept {
        return hook_ != nullptr;
    }
    [[nodiscard]] static ChromeWindowEvent DecodeMessage(
        WPARAM wparam,
        LPARAM lparam) noexcept;

    static void CALLBACK WinEventCallback(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG object_id,
        LONG child_id,
        DWORD event_thread,
        DWORD event_time) noexcept;

private:
    [[nodiscard]] bool Post(
        ChromeWindowEventKind kind,
        HWND hwnd) const noexcept;

    HWINEVENTHOOK hook_ = nullptr;
    DWORD target_thread_id_ = 0;
    UINT target_message_ = 0;
};

}  // namespace ctm
