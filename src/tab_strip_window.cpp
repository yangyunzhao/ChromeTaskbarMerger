#include "tab_strip_window.h"

#include <windowsx.h>

#include <algorithm>

namespace ctm {
namespace {

constexpr wchar_t kTabStripClassName[] =
    L"ChromeTaskbarMerger.V2.TabStrip";

[[nodiscard]] int RectangleWidth(const RECT& rectangle) noexcept {
    return rectangle.right - rectangle.left;
}

[[nodiscard]] int RectangleHeight(const RECT& rectangle) noexcept {
    return rectangle.bottom - rectangle.top;
}

void AssignError(DWORD* const error_code, const DWORD value) noexcept {
    if (error_code != nullptr) {
        *error_code = value;
    }
}

[[nodiscard]] bool RegisterTabStripClass(const HINSTANCE instance,
                                         DWORD* const error_code) {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kTabStripClassName, &existing) != FALSE) {
        AssignError(error_code, ERROR_SUCCESS);
        return true;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &TabStripWindow::WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kTabStripClassName;
    if (RegisterClassExW(&window_class) == 0) {
        const DWORD registration_error = GetLastError();
        if (registration_error != ERROR_CLASS_ALREADY_EXISTS) {
            AssignError(error_code, registration_error);
            return false;
        }
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

}  // namespace

TabStripWindow::~TabStripWindow() {
    Destroy();
}

bool TabStripWindow::Create(
    const HINSTANCE instance,
    const HWND owner,
    const RECT& bounds,
    const std::span<const TabStripItem> items,
    const WindowIdentity& active_identity,
    ITabStripEventSink* const event_sink,
    DWORD* const error_code) {
    if (hwnd_ != nullptr || instance == nullptr || owner == nullptr ||
        IsWindow(owner) == FALSE || event_sink == nullptr || items.empty() ||
        RectangleWidth(bounds) <= 0 || RectangleHeight(bounds) <= 0) {
        AssignError(error_code, ERROR_INVALID_PARAMETER);
        return false;
    }
    if (!RegisterTabStripClass(instance, error_code)) {
        return false;
    }

    instance_ = instance;
    event_sink_ = event_sink;
    items_.assign(items.begin(), items.end());
    active_identity_ = active_identity;
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kTabStripClassName,
        L"ChromeTaskbarMerger V2 tabs",
        WS_POPUP,
        bounds.left,
        bounds.top,
        RectangleWidth(bounds),
        RectangleHeight(bounds),
        owner,
        nullptr,
        instance,
        this);
    if (hwnd_ == nullptr) {
        const DWORD creation_error = GetLastError();
        items_.clear();
        event_sink_ = nullptr;
        instance_ = nullptr;
        AssignError(error_code, creation_error);
        return false;
    }

    DWORD item_error = ERROR_SUCCESS;
    if (!SetItems(items, active_identity, &item_error)) {
        Destroy();
        AssignError(error_code, item_error);
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    static_cast<void>(UpdateWindow(hwnd_));
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

bool TabStripWindow::SetItems(
    const std::span<const TabStripItem> items,
    const WindowIdentity& active_identity,
    DWORD* const error_code) {
    if (items.empty()) {
        AssignError(error_code, ERROR_INVALID_PARAMETER);
        return false;
    }
    std::vector<TabStripItem> validated;
    validated.reserve(items.size());
    for (const TabStripItem& item : items) {
        const bool duplicate = std::any_of(
            validated.begin(),
            validated.end(),
            [&item](const TabStripItem& existing) {
                return existing.identity.hwnd == item.identity.hwnd;
            });
        if (!WindowIdentityIsComplete(item.identity) || duplicate) {
            AssignError(error_code, ERROR_INVALID_DATA);
            return false;
        }
        validated.push_back(item);
    }
    const auto active = std::find_if(
        validated.begin(),
        validated.end(),
        [&active_identity](const TabStripItem& item) {
            return WindowIdentitiesMatch(
                item.identity, active_identity);
        });
    if (active == validated.end()) {
        AssignError(error_code, ERROR_NOT_FOUND);
        return false;
    }

    items_ = std::move(validated);
    active_identity_ = active_identity;
    RecalculateLayout();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

bool TabStripWindow::SetActive(const WindowIdentity& identity) noexcept {
    const auto match = std::find_if(
        items_.begin(),
        items_.end(),
        [&identity](const TabStripItem& item) {
            return WindowIdentitiesMatch(item.identity, identity);
        });
    if (match == items_.end()) {
        return false;
    }
    active_identity_ = identity;
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return true;
}

bool TabStripWindow::SetOwner(const HWND owner,
                              DWORD* const error_code) noexcept {
    if (hwnd_ == nullptr || owner == nullptr || IsWindow(owner) == FALSE) {
        AssignError(error_code, ERROR_INVALID_PARAMETER);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    const DWORD owner_error = GetLastError();
    if (previous == 0 && owner_error != ERROR_SUCCESS) {
        AssignError(error_code, owner_error);
        return false;
    }
    if (SetWindowPos(
            hwnd_,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) ==
        FALSE) {
        AssignError(error_code, GetLastError());
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

bool TabStripWindow::SetBounds(const RECT& bounds,
                               DWORD* const error_code) noexcept {
    if (hwnd_ == nullptr || RectangleWidth(bounds) <= 0 ||
        RectangleHeight(bounds) <= 0) {
        AssignError(error_code, ERROR_INVALID_PARAMETER);
        return false;
    }
    if (SetWindowPos(
            hwnd_,
            HWND_TOP,
            bounds.left,
            bounds.top,
            RectangleWidth(bounds),
            RectangleHeight(bounds),
            SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE) {
        AssignError(error_code, GetLastError());
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

void TabStripWindow::Destroy() noexcept {
    if (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    instance_ = nullptr;
    event_sink_ = nullptr;
    items_.clear();
    active_identity_ = {};
    layout_ = {};
}

bool TabStripWindow::IsHealthy() const noexcept {
    return hwnd_ != nullptr && IsWindow(hwnd_) != FALSE &&
           event_sink_ != nullptr && !items_.empty() &&
           layout_.items.size() == items_.size();
}

LRESULT CALLBACK TabStripWindow::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) noexcept {
    TabStripWindow* self = reinterpret_cast<TabStripWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* const creation =
            reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<TabStripWindow*>(creation->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = window;
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    if (self == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const LRESULT result = self->HandleMessage(message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

LRESULT TabStripWindow::HandleMessage(const UINT message,
                                      const WPARAM wparam,
                                      const LPARAM lparam) noexcept {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_SIZE:
            RecalculateLayout();
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_LBUTTONUP: {
            const POINT point = {
                .x = GET_X_LPARAM(lparam),
                .y = GET_Y_LPARAM(lparam),
            };
            const TabHitResult hit = HitTestTabStrip(layout_, point);
            if (hit.region == TabHitRegion::None ||
                hit.index >= items_.size() || event_sink_ == nullptr) {
                return 0;
            }
            const WindowIdentity identity = items_[hit.index].identity;
            if (hit.region == TabHitRegion::Close) {
                event_sink_->OnTabCloseRequested(identity);
            } else {
                event_sink_->OnTabActivationRequested(identity);
            }
            return 0;
        }
        case WM_NCDESTROY:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
        default:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void TabStripWindow::RecalculateLayout() noexcept {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT client{};
    if (GetClientRect(hwnd_, &client) == FALSE) {
        layout_ = {};
        return;
    }
    layout_ = CalculateTabStripLayout(
        {.cx = client.right - client.left,
         .cy = client.bottom - client.top},
        items_.size());
}

void TabStripWindow::Paint() noexcept {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(hwnd_, &paint);
    if (device == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    const HBRUSH background = CreateSolidBrush(RGB(28, 31, 38));
    if (background != nullptr) {
        FillRect(device, &client, background);
        DeleteObject(background);
    }
    SetBkMode(device, TRANSPARENT);
    const HGDIOBJ previous_font =
        SelectObject(device, GetStockObject(DEFAULT_GUI_FONT));

    for (std::size_t index = 0;
         index < items_.size() && index < layout_.items.size();
         ++index) {
        const TabLayoutItem& layout_item = layout_.items[index];
        const bool active = WindowIdentitiesMatch(
            items_[index].identity, active_identity_);
        const HBRUSH tab_brush = CreateSolidBrush(
            active ? RGB(45, 112, 214) : RGB(55, 60, 70));
        if (tab_brush != nullptr) {
            FillRect(device, &layout_item.bounds, tab_brush);
            DeleteObject(tab_brush);
        }

        RECT text_bounds = layout_item.bounds;
        text_bounds.left += 9;
        text_bounds.right = layout_item.close_bounds.right >
                                    layout_item.close_bounds.left
                                ? layout_item.close_bounds.left - 4
                                : text_bounds.right - 8;
        SetTextColor(device, RGB(245, 247, 250));
        const std::wstring& title = items_[index].title;
        DrawTextW(
            device,
            title.empty() ? L"Chrome" : title.c_str(),
            -1,
            &text_bounds,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (layout_item.close_bounds.right >
            layout_item.close_bounds.left) {
            RECT close_bounds = layout_item.close_bounds;
            SetTextColor(device, RGB(230, 233, 238));
            DrawTextW(
                device,
                L"\u00D7",
                1,
                &close_bounds,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
    }

    if (previous_font != nullptr) {
        SelectObject(device, previous_font);
    }
    EndPaint(hwnd_, &paint);
}

}  // namespace ctm
