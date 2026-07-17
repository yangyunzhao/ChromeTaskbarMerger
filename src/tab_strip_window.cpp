#include "tab_strip_window.h"

#include "tab_name_store.h"

#include <CommCtrl.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <vector>

namespace ctm {
namespace {

constexpr wchar_t kTabStripClassName[] =
    L"ChromeTaskbarMerger.V2.TabStrip";
constexpr UINT kEndNameEditMessage = WM_APP + 0x37;
constexpr WPARAM kCancelAndReactivate = 0;
constexpr WPARAM kCommitAndReactivate = 1;
constexpr WPARAM kCommitWithoutReactivation = 2;
constexpr UINT_PTR kNameEditorSubclassId = 1;

[[nodiscard]] int RectangleWidth(const RECT& rectangle) noexcept {
    return rectangle.right - rectangle.left;
}

[[nodiscard]] int RectangleHeight(const RECT& rectangle) noexcept {
    return rectangle.bottom - rectangle.top;
}

[[nodiscard]] bool HasActiveImeComposition(const HWND window) noexcept {
    const HIMC context = ImmGetContext(window);
    if (context == nullptr) {
        return false;
    }
    const LONG bytes = ImmGetCompositionStringW(
        context, GCS_COMPSTR, nullptr, 0);
    ImmReleaseContext(window, context);
    return bytes > 0;
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
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }
    UpdateDpiResources();

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

    if (name_editor_ != nullptr) {
        const auto edited = std::find_if(
            validated.begin(),
            validated.end(),
            [this](const TabStripItem& item) {
                return WindowIdentitiesMatch(
                    item.identity, editing_identity_);
            });
        if (edited == validated.end()) {
            EndNameEdit(false, false);
        }
    }

    items_ = std::move(validated);
    active_identity_ = active_identity;
    RecalculateLayout();
    EnsureActiveVisible();
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
    EnsureActiveVisible();
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
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) ==
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
            SWP_NOACTIVATE) == FALSE) {
        AssignError(error_code, GetLastError());
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

bool TabStripWindow::SetVisible(const bool visible,
                                DWORD* const error_code) noexcept {
    if (hwnd_ == nullptr || IsWindow(hwnd_) == FALSE) {
        AssignError(error_code, ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (!visible && name_editor_ != nullptr) {
        EndNameEdit(true, false);
    }
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if ((IsWindowVisible(hwnd_) != FALSE) != visible) {
        AssignError(error_code, ERROR_INVALID_STATE);
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

void TabStripWindow::Destroy() noexcept {
    if (name_editor_ != nullptr) {
        EndNameEdit(false, false);
    }
    if (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    instance_ = nullptr;
    event_sink_ = nullptr;
    items_.clear();
    active_identity_ = {};
    layout_ = {};
    dpi_ = USER_DEFAULT_SCREEN_DPI;
    editing_identity_ = {};
    edit_end_pending_ = false;
    suppress_click_up_ = false;
    first_visible_index_ = 0;
    wheel_delta_remainder_ = 0;
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
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
            return name_editor_ != nullptr ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_SIZE:
            RecalculateLayout();
            EnsureActiveVisible();
            return 0;
        case WM_DPICHANGED:
            dpi_ = LOWORD(wparam);
            UpdateDpiResources();
            RecalculateLayout();
            EnsureActiveVisible();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_MOUSEWHEEL:
            wheel_delta_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
            if (const int steps = wheel_delta_remainder_ / WHEEL_DELTA;
                steps != 0) {
                wheel_delta_remainder_ -= steps * WHEEL_DELTA;
                ScrollByTabs(-steps);
            }
            return 0;
        case WM_MOUSEHWHEEL:
            wheel_delta_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
            if (const int steps = wheel_delta_remainder_ / WHEEL_DELTA;
                steps != 0) {
                wheel_delta_remainder_ -= steps * WHEEL_DELTA;
                ScrollByTabs(steps);
            }
            return 0;
        case WM_LBUTTONDBLCLK: {
            const POINT point = {
                .x = GET_X_LPARAM(lparam),
                .y = GET_Y_LPARAM(lparam),
            };
            const TabHitResult hit = HitTestTabStrip(layout_, point);
            if (hit.region == TabHitRegion::Body &&
                hit.index < items_.size() && BeginNameEdit(hit.index)) {
                suppress_click_up_ = true;
            } else if (hit.region == TabHitRegion::Close) {
                suppress_click_up_ = true;
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (suppress_click_up_) {
                suppress_click_up_ = false;
                return 0;
            }
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
        case kEndNameEditMessage:
            if (name_editor_ != nullptr) {
                const bool commit = wparam != kCancelAndReactivate;
                const bool reactivate =
                    wparam != kCommitWithoutReactivation;
                EndNameEdit(commit, reactivate);
            }
            return 0;
        case WM_NCDESTROY:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
        default:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

bool TabStripWindow::BeginNameEdit(const std::size_t index) noexcept {
    if (hwnd_ == nullptr || name_editor_ != nullptr ||
        index >= items_.size() || index >= layout_.items.size() ||
        event_sink_ == nullptr) {
        return false;
    }
    RECT bounds = layout_.items[index].bounds;
    const RECT close = layout_.items[index].close_bounds;
    const int inset = MulDiv(
        3, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    bounds.left += inset;
    bounds.top += inset;
    bounds.bottom -= inset;
    bounds.right = close.right > close.left
                       ? close.left - inset
                       : bounds.right - inset;
    if (bounds.right - bounds.left < MulDiv(
            24, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI) ||
        bounds.bottom <= bounds.top) {
        return false;
    }

    HWND editor = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        items_[index].title.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        hwnd_,
        nullptr,
        instance_,
        nullptr);
    if (editor == nullptr) {
        return false;
    }
    if (SetWindowSubclass(
            editor,
            &TabStripWindow::EditWindowProcedure,
            kNameEditorSubclassId,
            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        DestroyWindow(editor);
        return false;
    }

    name_editor_ = editor;
    editing_identity_ = items_[index].identity;
    edit_end_pending_ = false;
    SendMessageW(
        name_editor_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
    SendMessageW(
        name_editor_,
        EM_SETLIMITTEXT,
        static_cast<WPARAM>(kMaximumInMemoryTabNameLength),
        0);
    SendMessageW(name_editor_, EM_SETSEL, 0, -1);
    SetEditingActivationMode(true);
    static_cast<void>(SetForegroundWindow(hwnd_));
    static_cast<void>(SetActiveWindow(hwnd_));
    static_cast<void>(SetFocus(name_editor_));
    return true;
}

void TabStripWindow::EndNameEdit(const bool commit,
                                 const bool reactivate) noexcept {
    if (name_editor_ == nullptr) {
        return;
    }
    std::wstring name;
    if (commit) {
        const int length = GetWindowTextLengthW(name_editor_);
        if (length > 0) {
            std::vector<wchar_t> buffer(
                static_cast<std::size_t>(length) + 1U, L'\0');
            const int copied = GetWindowTextW(
                name_editor_, buffer.data(), static_cast<int>(buffer.size()));
            if (copied > 0) {
                name.assign(buffer.data(), static_cast<std::size_t>(copied));
            }
        }
    }
    const WindowIdentity identity = editing_identity_;
    HWND editor = name_editor_;
    name_editor_ = nullptr;
    editing_identity_ = {};
    edit_end_pending_ = false;
    static_cast<void>(RemoveWindowSubclass(
        editor,
        &TabStripWindow::EditWindowProcedure,
        kNameEditorSubclassId));
    if (IsWindow(editor) != FALSE) {
        DestroyWindow(editor);
    }
    SetEditingActivationMode(false);
    InvalidateRect(hwnd_, nullptr, FALSE);

    ITabStripEventSink* const sink = event_sink_;
    if (commit && sink != nullptr) {
        sink->OnTabNameChangeRequested(identity, name);
    }
    if (reactivate && sink != nullptr) {
        sink->OnTabActivationRequested(identity);
    }
}

void TabStripWindow::RepositionNameEditor() noexcept {
    if (name_editor_ == nullptr) {
        return;
    }
    const auto edited = std::find_if(
        items_.begin(),
        items_.end(),
        [this](const TabStripItem& item) {
            return WindowIdentitiesMatch(item.identity, editing_identity_);
        });
    if (edited == items_.end()) {
        EndNameEdit(false, false);
        return;
    }
    const std::size_t index = static_cast<std::size_t>(
        std::distance(items_.begin(), edited));
    if (index >= layout_.items.size()) {
        EndNameEdit(false, false);
        return;
    }
    RECT bounds = layout_.items[index].bounds;
    const RECT close = layout_.items[index].close_bounds;
    const int inset = MulDiv(
        3, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    bounds.left += inset;
    bounds.top += inset;
    bounds.bottom -= inset;
    bounds.right = close.right > close.left
                       ? close.left - inset
                       : bounds.right - inset;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        EndNameEdit(false, false);
        return;
    }
    static_cast<void>(SetWindowPos(
        name_editor_,
        HWND_TOP,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOACTIVATE));
}

void TabStripWindow::SetEditingActivationMode(
    const bool editing) noexcept {
    if (hwnd_ == nullptr || IsWindow(hwnd_) == FALSE) {
        return;
    }
    LONG_PTR extended_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    extended_style = editing
                         ? extended_style & ~WS_EX_NOACTIVATE
                         : extended_style | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, extended_style);
    static_cast<void>(SetWindowPos(
        hwnd_,
        HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED));
}

void TabStripWindow::ScrollByTabs(const int delta) noexcept {
    if (!layout_.overflowed || layout_.visible_capacity == 0 ||
        items_.size() <= layout_.visible_capacity || delta == 0) {
        return;
    }
    const std::size_t maximum_first =
        items_.size() - layout_.visible_capacity;
    const long long requested =
        static_cast<long long>(first_visible_index_) + delta;
    first_visible_index_ = static_cast<std::size_t>(std::clamp<long long>(
        requested, 0, static_cast<long long>(maximum_first)));
    RecalculateLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void TabStripWindow::EnsureActiveVisible() noexcept {
    if (!layout_.overflowed || layout_.visible_capacity == 0) {
        return;
    }
    const auto active = std::find_if(
        items_.begin(),
        items_.end(),
        [this](const TabStripItem& item) {
            return WindowIdentitiesMatch(item.identity, active_identity_);
        });
    if (active == items_.end()) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(
        std::distance(items_.begin(), active));
    std::size_t desired = first_visible_index_;
    if (index < desired) {
        desired = index;
    } else if (index >= desired + layout_.visible_capacity) {
        desired = index - layout_.visible_capacity + 1U;
    }
    if (desired == first_visible_index_) {
        return;
    }
    first_visible_index_ = desired;
    RecalculateLayout();
}

LRESULT CALLBACK TabStripWindow::EditWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam,
    const UINT_PTR subclass_id,
    const DWORD_PTR reference_data) noexcept {
    auto* const self = reinterpret_cast<TabStripWindow*>(reference_data);
    if (self == nullptr || subclass_id != kNameEditorSubclassId) {
        return DefSubclassProc(window, message, wparam, lparam);
    }
    if (message == WM_GETDLGCODE) {
        return DefSubclassProc(window, message, wparam, lparam) |
               DLGC_WANTALLKEYS;
    }
    if (message == WM_KEYDOWN && !self->edit_end_pending_) {
        if (wparam == VK_RETURN || wparam == VK_ESCAPE) {
            if (HasActiveImeComposition(window)) {
                return DefSubclassProc(window, message, wparam, lparam);
            }
            self->edit_end_pending_ = true;
            if (PostMessageW(
                    self->hwnd_,
                    kEndNameEditMessage,
                    wparam == VK_RETURN ? kCommitAndReactivate
                                        : kCancelAndReactivate,
                    0) == FALSE) {
                self->edit_end_pending_ = false;
                MessageBeep(MB_ICONWARNING);
            }
            return 0;
        }
    }
    if (message == WM_KILLFOCUS && !self->edit_end_pending_) {
        self->edit_end_pending_ = true;
        if (PostMessageW(
                self->hwnd_,
                kEndNameEditMessage,
                kCommitWithoutReactivation,
                0) == FALSE) {
            self->edit_end_pending_ = false;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
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
        items_.size(),
        dpi_,
        maximum_tab_width_pixels_,
        first_visible_index_);
    first_visible_index_ = layout_.first_visible_index;
    RepositionNameEditor();
}

void TabStripWindow::SetMaximumTabWidth(
    const int logical_pixels) noexcept {
    maximum_tab_width_pixels_ = std::clamp(
        logical_pixels, kMinimumTabWidthPixels, kMaximumTabWidthPixels);
    RecalculateLayout();
    EnsureActiveVisible();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabStripWindow::UpdateDpiResources() noexcept {
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0,
            dpi_) != FALSE) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (name_editor_ != nullptr) {
        SendMessageW(
            name_editor_,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(
                font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT)),
            TRUE);
    }
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
        SelectObject(
            device,
            font_ != nullptr ? static_cast<HGDIOBJ>(font_)
                             : GetStockObject(DEFAULT_GUI_FONT));

    const int saved_device = SaveDC(device);
    if (layout_.viewport_bounds.right > layout_.viewport_bounds.left &&
        layout_.viewport_bounds.bottom > layout_.viewport_bounds.top) {
        IntersectClipRect(
            device,
            layout_.viewport_bounds.left,
            layout_.viewport_bounds.top,
            layout_.viewport_bounds.right,
            layout_.viewport_bounds.bottom);
    }

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
        text_bounds.left += MulDiv(
            9, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
        text_bounds.right = layout_item.close_bounds.right >
                                    layout_item.close_bounds.left
                                ? layout_item.close_bounds.left -
                                      MulDiv(
                                          4,
                                          static_cast<int>(dpi_),
                                          USER_DEFAULT_SCREEN_DPI)
                                : text_bounds.right -
                                      MulDiv(
                                          8,
                                          static_cast<int>(dpi_),
                                          USER_DEFAULT_SCREEN_DPI);
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

    if (saved_device != 0) {
        RestoreDC(device, saved_device);
    }

    if (layout_.overflowed && layout_.visible_capacity != 0) {
        const int indicator_width = MulDiv(
            16, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
        SetTextColor(device, RGB(245, 247, 250));
        if (layout_.first_visible_index > 0) {
            RECT left_indicator = layout_.viewport_bounds;
            left_indicator.right = std::min(
                left_indicator.right,
                left_indicator.left + indicator_width);
            DrawTextW(
                device,
                L"\u2039",
                1,
                &left_indicator,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        if (layout_.first_visible_index + layout_.visible_capacity <
            items_.size()) {
            RECT right_indicator = layout_.viewport_bounds;
            right_indicator.left = std::max(
                right_indicator.left,
                right_indicator.right - indicator_width);
            DrawTextW(
                device,
                L"\u203A",
                1,
                &right_indicator,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
    }

    if (previous_font != nullptr) {
        SelectObject(device, previous_font);
    }
    EndPaint(hwnd_, &paint);
}

}  // namespace ctm
