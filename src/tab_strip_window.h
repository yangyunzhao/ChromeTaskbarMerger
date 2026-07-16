#pragma once

#include "tab_strip_layout.h"
#include "window_identity.h"

#include <Windows.h>

#include <span>
#include <string>
#include <vector>

namespace ctm {

struct TabStripItem {
    WindowIdentity identity;
    std::wstring title;
};

class ITabStripEventSink {
public:
    virtual ~ITabStripEventSink() = default;

    virtual void OnTabActivationRequested(
        const WindowIdentity& identity) = 0;
    virtual void OnTabCloseRequested(
        const WindowIdentity& identity) = 0;
};

class TabStripWindow final {
public:
    TabStripWindow() = default;
    ~TabStripWindow();

    TabStripWindow(const TabStripWindow&) = delete;
    TabStripWindow& operator=(const TabStripWindow&) = delete;
    TabStripWindow(TabStripWindow&&) = delete;
    TabStripWindow& operator=(TabStripWindow&&) = delete;

    [[nodiscard]] bool Create(
        HINSTANCE instance,
        HWND owner,
        const RECT& bounds,
        std::span<const TabStripItem> items,
        const WindowIdentity& active_identity,
        ITabStripEventSink* event_sink,
        DWORD* error_code);
    [[nodiscard]] bool SetItems(
        std::span<const TabStripItem> items,
        const WindowIdentity& active_identity,
        DWORD* error_code);
    [[nodiscard]] bool SetActive(
        const WindowIdentity& identity) noexcept;
    [[nodiscard]] bool SetOwner(HWND owner, DWORD* error_code) noexcept;
    [[nodiscard]] bool SetBounds(
        const RECT& bounds,
        DWORD* error_code) noexcept;
    void Destroy() noexcept;

    [[nodiscard]] bool IsHealthy() const noexcept;
    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }
    [[nodiscard]] const TabStripLayout& layout() const noexcept {
        return layout_;
    }

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;

private:
    [[nodiscard]] LRESULT HandleMessage(
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;
    void RecalculateLayout() noexcept;
    void Paint() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ITabStripEventSink* event_sink_ = nullptr;
    std::vector<TabStripItem> items_;
    WindowIdentity active_identity_;
    TabStripLayout layout_;
};

}  // namespace ctm
