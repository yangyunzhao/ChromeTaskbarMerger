#pragma once

#include "tab_strip_layout.h"
#include "window_identity.h"

#include <Windows.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ID2D1Factory;
struct ID2D1DCRenderTarget;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteInlineObject;
struct IWICImagingFactory;

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
    virtual void OnTabNameChangeRequested(
        const WindowIdentity& identity,
        std::wstring_view name) = 0;
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
    [[nodiscard]] bool SetVisible(
        bool visible,
        DWORD* error_code) noexcept;
    void SetMaximumTabWidth(int logical_pixels) noexcept;
    void SetContentAlignment(TabStripAlignment alignment) noexcept;
    void SetSurfaceMode(TabStripSurfaceMode mode) noexcept;
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
    [[nodiscard]] bool BeginNameEdit(std::size_t index) noexcept;
    void EndNameEdit(bool commit, bool reactivate) noexcept;
    void RepositionNameEditor() noexcept;
    void SetEditingActivationMode(bool editing) noexcept;
    void ScrollByTabs(int delta) noexcept;
    void EnsureActiveVisible() noexcept;
    void RecalculateLayout() noexcept;
    [[nodiscard]] bool ApplyAvailableBounds(
        DWORD* error_code) noexcept;
    [[nodiscard]] bool EnsureGraphicsResources() noexcept;
    void ReleaseDeviceResources() noexcept;
    void ReleaseGraphicsResources() noexcept;
    void UpdateDpiResources() noexcept;
    void Paint() noexcept;

    static LRESULT CALLBACK EditWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR subclass_id,
        DWORD_PTR reference_data) noexcept;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ITabStripEventSink* event_sink_ = nullptr;
    std::vector<TabStripItem> items_;
    WindowIdentity active_identity_;
    TabStripLayout layout_;
    RECT available_bounds_{};
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    int maximum_tab_width_pixels_ = kDefaultTabWidthPixels;
    TabStripAlignment content_alignment_ = TabStripAlignment::Left;
    TabStripSurfaceMode surface_mode_ =
        TabStripSurfaceMode::AttachedAbove;
    HFONT font_ = nullptr;
    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1DCRenderTarget* render_target_ = nullptr;
    HDC surface_dc_ = nullptr;
    HBITMAP surface_bitmap_ = nullptr;
    HGDIOBJ previous_surface_bitmap_ = nullptr;
    SIZE surface_size_{};
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
    IDWriteInlineObject* trimming_sign_ = nullptr;
    IWICImagingFactory* wic_factory_ = nullptr;
    HWND name_editor_ = nullptr;
    WindowIdentity editing_identity_;
    bool edit_end_pending_ = false;
    bool suppress_click_up_ = false;
    std::size_t first_visible_index_ = 0;
    int wheel_delta_remainder_ = 0;
};

}  // namespace ctm
