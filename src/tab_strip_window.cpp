#include "tab_strip_window.h"

#include "tab_name_store.h"

#include <CommCtrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <imm.h>
#include <wincodec.h>
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

template <typename Interface>
void SafeRelease(Interface** const value) noexcept {
    if (value != nullptr && *value != nullptr) {
        (*value)->Release();
        *value = nullptr;
    }
}

[[nodiscard]] HRESULT CreateTopRoundedGeometry(
    ID2D1Factory* const factory,
    const D2D1_RECT_F bounds,
    const FLOAT radius,
    const bool outline_only,
    ID2D1PathGeometry** const output) noexcept {
    if (factory == nullptr || output == nullptr ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return E_INVALIDARG;
    }
    *output = nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    HRESULT result = factory->CreatePathGeometry(&geometry);
    if (FAILED(result)) {
        return result;
    }
    ID2D1GeometrySink* sink = nullptr;
    result = geometry->Open(&sink);
    if (SUCCEEDED(result)) {
        const FLOAT safe_radius = std::clamp(
            radius,
            1.0F,
            std::min(
                (bounds.right - bounds.left) / 2.0F,
                bounds.bottom - bounds.top));
        sink->BeginFigure(
            D2D1::Point2F(bounds.left, bounds.bottom),
            outline_only ? D2D1_FIGURE_BEGIN_HOLLOW
                         : D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(
            bounds.left, bounds.top + safe_radius));
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(bounds.left + safe_radius, bounds.top),
            D2D1::SizeF(safe_radius, safe_radius),
            0.0F,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(
            bounds.right - safe_radius, bounds.top));
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(bounds.right, bounds.top + safe_radius),
            D2D1::SizeF(safe_radius, safe_radius),
            0.0F,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(bounds.right, bounds.bottom));
        sink->EndFigure(
            outline_only ? D2D1_FIGURE_END_OPEN
                         : D2D1_FIGURE_END_CLOSED);
        result = sink->Close();
    }
    SafeRelease(&sink);
    if (FAILED(result)) {
        SafeRelease(&geometry);
        return result;
    }
    *output = geometry;
    return S_OK;
}

[[nodiscard]] ID2D1Bitmap* CreateIconBitmap(
    IWICImagingFactory* const wic_factory,
    ID2D1RenderTarget* const render_target,
    const HICON icon) noexcept {
    if (wic_factory == nullptr || render_target == nullptr || icon == nullptr) {
        return nullptr;
    }
    IWICBitmap* source = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    HRESULT result = wic_factory->CreateBitmapFromHICON(icon, &source);
    if (SUCCEEDED(result)) {
        result = wic_factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(result)) {
        result = converter->Initialize(
            source,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result)) {
        result = render_target->CreateBitmapFromWicBitmap(
            converter, nullptr, &bitmap);
    }
    SafeRelease(&converter);
    SafeRelease(&source);
    return SUCCEEDED(result) ? bitmap : nullptr;
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
    available_bounds_ = bounds;
    dpi_ = GetDpiForWindow(owner);
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }
    const RECT compact_bounds = CalculateCompactTabStripBounds(
        available_bounds_,
        items_.size(),
        dpi_,
        maximum_tab_width_pixels_,
        content_alignment_);
    if (RectangleWidth(compact_bounds) <= 0 ||
        RectangleHeight(compact_bounds) <= 0) {
        items_.clear();
        event_sink_ = nullptr;
        instance_ = nullptr;
        available_bounds_ = {};
        AssignError(error_code, ERROR_INVALID_DATA);
        return false;
    }
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kTabStripClassName,
        L"ChromeTaskbarMerger V2 tabs",
        WS_POPUP,
        compact_bounds.left,
        compact_bounds.top,
        RectangleWidth(compact_bounds),
        RectangleHeight(compact_bounds),
        owner,
        nullptr,
        instance,
        this);
    if (hwnd_ == nullptr) {
        const DWORD creation_error = GetLastError();
        items_.clear();
        event_sink_ = nullptr;
        instance_ = nullptr;
        available_bounds_ = {};
        AssignError(error_code, creation_error);
        return false;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }
    UpdateDpiResources();
    // The tab silhouette and transparency are rendered per pixel. Asking DWM
    // not to add an opaque outer corner or non-client shadow keeps the visible
    // right edge inside Chrome's own window rectangle.
    const DWM_WINDOW_CORNER_PREFERENCE corner_preference =
        DWMWCP_DONOTROUND;
    static_cast<void>(DwmSetWindowAttribute(
        hwnd_,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &corner_preference,
        sizeof(corner_preference)));
    const DWMNCRENDERINGPOLICY non_client_policy = DWMNCRP_DISABLED;
    static_cast<void>(DwmSetWindowAttribute(
        hwnd_,
        DWMWA_NCRENDERING_POLICY,
        &non_client_policy,
        sizeof(non_client_policy)));

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
    DWORD bounds_error = ERROR_SUCCESS;
    if (hwnd_ != nullptr && !ApplyAvailableBounds(&bounds_error)) {
        AssignError(error_code, bounds_error);
        return false;
    }
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
    available_bounds_ = bounds;
    return ApplyAvailableBounds(error_code);
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
    ReleaseGraphicsResources();
    instance_ = nullptr;
    event_sink_ = nullptr;
    items_.clear();
    active_identity_ = {};
    layout_ = {};
    available_bounds_ = {};
    dpi_ = USER_DEFAULT_SCREEN_DPI;
    editing_identity_ = {};
    edit_end_pending_ = false;
    suppress_click_up_ = false;
    first_visible_index_ = 0;
    wheel_delta_remainder_ = 0;
    surface_mode_ = TabStripSurfaceMode::AttachedAbove;
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
            ReleaseDeviceResources();
            RecalculateLayout();
            EnsureActiveVisible();
            return 0;
        case WM_DPICHANGED:
            dpi_ = LOWORD(wparam);
            ReleaseGraphicsResources();
            UpdateDpiResources();
            static_cast<void>(ApplyAvailableBounds(nullptr));
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
        first_visible_index_,
        content_alignment_);
    first_visible_index_ = layout_.first_visible_index;
    RepositionNameEditor();
}

bool TabStripWindow::ApplyAvailableBounds(
    DWORD* const error_code) noexcept {
    if (hwnd_ == nullptr || RectangleWidth(available_bounds_) <= 0 ||
        RectangleHeight(available_bounds_) <= 0 || items_.empty()) {
        AssignError(error_code, ERROR_INVALID_STATE);
        return false;
    }
    RECT visible_available_bounds = available_bounds_;
    const HWND owner = GetWindow(hwnd_, GW_OWNER);
    RECT owner_window_bounds{};
    RECT owner_visible_bounds{};
    if (owner != nullptr &&
        GetWindowRect(owner, &owner_window_bounds) != FALSE &&
        SUCCEEDED(DwmGetWindowAttribute(
            owner,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &owner_visible_bounds,
            sizeof(owner_visible_bounds)))) {
        visible_available_bounds = AdjustTabStripBoundsForInvisibleFrame(
            available_bounds_, owner_window_bounds, owner_visible_bounds);
    }
    const RECT compact = CalculateCompactTabStripBounds(
        visible_available_bounds,
        items_.size(),
        dpi_,
        maximum_tab_width_pixels_,
        content_alignment_);
    if (RectangleWidth(compact) <= 0 || RectangleHeight(compact) <= 0) {
        AssignError(error_code, ERROR_INVALID_DATA);
        return false;
    }
    if (SetWindowPos(
            hwnd_,
            HWND_TOP,
            compact.left,
            compact.top,
            RectangleWidth(compact),
            RectangleHeight(compact),
            SWP_NOACTIVATE) == FALSE) {
        AssignError(error_code, GetLastError());
        return false;
    }
    AssignError(error_code, ERROR_SUCCESS);
    return true;
}

void TabStripWindow::SetMaximumTabWidth(
    const int logical_pixels) noexcept {
    maximum_tab_width_pixels_ = std::clamp(
        logical_pixels, kMinimumTabWidthPixels, kMaximumTabWidthPixels);
    if (hwnd_ != nullptr) {
        static_cast<void>(ApplyAvailableBounds(nullptr));
    }
    RecalculateLayout();
    EnsureActiveVisible();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabStripWindow::SetSurfaceMode(
    const TabStripSurfaceMode mode) noexcept {
    if (surface_mode_ == mode) {
        return;
    }
    surface_mode_ = mode;
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabStripWindow::SetContentAlignment(
    const TabStripAlignment alignment) noexcept {
    if (content_alignment_ == alignment) {
        return;
    }
    content_alignment_ = alignment;
    if (hwnd_ != nullptr) {
        static_cast<void>(ApplyAvailableBounds(nullptr));
    }
    RecalculateLayout();
    EnsureActiveVisible();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool TabStripWindow::EnsureGraphicsResources() noexcept {
    if (hwnd_ == nullptr) {
        return false;
    }
    HRESULT result = S_OK;
    if (d2d_factory_ == nullptr) {
        result = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    }
    if (SUCCEEDED(result) && dwrite_factory_ == nullptr) {
        result = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&dwrite_factory_));
    }
    if (SUCCEEDED(result) && text_format_ == nullptr) {
        const FLOAT font_size = 12.0F * static_cast<FLOAT>(dpi_) /
                                USER_DEFAULT_SCREEN_DPI;
        result = dwrite_factory_->CreateTextFormat(
            L"Segoe UI Variable Text",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            font_size,
            L"zh-CN",
            &text_format_);
        if (SUCCEEDED(result)) {
            static_cast<void>(text_format_->SetTextAlignment(
                DWRITE_TEXT_ALIGNMENT_LEADING));
            static_cast<void>(text_format_->SetParagraphAlignment(
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
            static_cast<void>(text_format_->SetWordWrapping(
                DWRITE_WORD_WRAPPING_NO_WRAP));
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            static_cast<void>(dwrite_factory_->CreateEllipsisTrimmingSign(
                text_format_, &trimming_sign_));
            static_cast<void>(text_format_->SetTrimming(
                &trimming, trimming_sign_));
        }
    }
    if (SUCCEEDED(result) && surface_dc_ == nullptr) {
        RECT client{};
        if (GetClientRect(hwnd_, &client) == FALSE) {
            return false;
        }
        const int width = std::max(client.right - client.left, 1L);
        const int height = std::max(client.bottom - client.top, 1L);
        surface_dc_ = CreateCompatibleDC(nullptr);
        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
        bitmap_info.bmiHeader.biWidth = width;
        bitmap_info.bmiHeader.biHeight = -height;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        void* surface_bits = nullptr;
        if (surface_dc_ != nullptr) {
            surface_bitmap_ = CreateDIBSection(
                surface_dc_,
                &bitmap_info,
                DIB_RGB_COLORS,
                &surface_bits,
                nullptr,
                0);
        }
        if (surface_bitmap_ != nullptr) {
            previous_surface_bitmap_ = SelectObject(
                surface_dc_, surface_bitmap_);
            if (previous_surface_bitmap_ == nullptr ||
                previous_surface_bitmap_ == HGDI_ERROR) {
                previous_surface_bitmap_ = nullptr;
                result = E_OUTOFMEMORY;
            }
        } else {
            result = E_OUTOFMEMORY;
        }
        surface_size_ = {.cx = width, .cy = height};
    }
    if (SUCCEEDED(result) && render_target_ == nullptr) {
        const D2D1_RENDER_TARGET_PROPERTIES properties =
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    D2D1_ALPHA_MODE_PREMULTIPLIED),
                static_cast<FLOAT>(USER_DEFAULT_SCREEN_DPI),
                static_cast<FLOAT>(USER_DEFAULT_SCREEN_DPI));
        result = d2d_factory_->CreateDCRenderTarget(
            &properties, &render_target_);
        if (SUCCEEDED(result)) {
            render_target_->SetAntialiasMode(
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            render_target_->SetTextAntialiasMode(
                D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
    }
    if (wic_factory_ == nullptr) {
        static_cast<void>(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wic_factory_)));
    }
    if (FAILED(result)) {
        ReleaseDeviceResources();
    }
    return SUCCEEDED(result) && d2d_factory_ != nullptr &&
           dwrite_factory_ != nullptr && text_format_ != nullptr &&
           render_target_ != nullptr && surface_dc_ != nullptr &&
           surface_bitmap_ != nullptr;
}

void TabStripWindow::ReleaseDeviceResources() noexcept {
    SafeRelease(&render_target_);
    if (surface_dc_ != nullptr && previous_surface_bitmap_ != nullptr) {
        static_cast<void>(SelectObject(
            surface_dc_, previous_surface_bitmap_));
    }
    previous_surface_bitmap_ = nullptr;
    if (surface_bitmap_ != nullptr) {
        DeleteObject(surface_bitmap_);
        surface_bitmap_ = nullptr;
    }
    if (surface_dc_ != nullptr) {
        DeleteDC(surface_dc_);
        surface_dc_ = nullptr;
    }
    surface_size_ = {};
}

void TabStripWindow::ReleaseGraphicsResources() noexcept {
    ReleaseDeviceResources();
    SafeRelease(&trimming_sign_);
    SafeRelease(&text_format_);
    SafeRelease(&wic_factory_);
    SafeRelease(&dwrite_factory_);
    SafeRelease(&d2d_factory_);
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
    if (!EnsureGraphicsResources()) {
        const COLORREF fallback_surface =
            surface_mode_ == TabStripSurfaceMode::MaximizedOverlay
                ? RGB(211, 227, 253)
                : RGB(239, 245, 255);
        const HBRUSH background = CreateSolidBrush(fallback_surface);
        if (background != nullptr) {
            FillRect(device, &client, background);
            DeleteObject(background);
        }
        SetBkMode(device, TRANSPARENT);
        SetTextColor(device, RGB(32, 33, 36));
        const HGDIOBJ previous_font = SelectObject(
            device,
            font_ != nullptr ? static_cast<HGDIOBJ>(font_)
                             : GetStockObject(DEFAULT_GUI_FONT));
        for (std::size_t index = 0;
             index < items_.size() && index < layout_.items.size();
             ++index) {
            RECT text_bounds = layout_.items[index].bounds;
            text_bounds.left += 8;
            text_bounds.right -= 24;
            const std::wstring& title = items_[index].title;
            DrawTextW(
                device,
                title.empty() ? L"Chrome" : title.c_str(),
                -1,
                &text_bounds,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
        }
        if (previous_font != nullptr) {
            SelectObject(device, previous_font);
        }
        EndPaint(hwnd_, &paint);
        return;
    }

    ID2D1SolidColorBrush* active_fill = nullptr;
    ID2D1SolidColorBrush* inactive_fill = nullptr;
    ID2D1SolidColorBrush* active_border = nullptr;
    ID2D1SolidColorBrush* inactive_border = nullptr;
    ID2D1SolidColorBrush* text_brush = nullptr;
    ID2D1SolidColorBrush* shadow_brush = nullptr;
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0xA8D0F8), &active_fill));
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0xFAFCFF), &inactive_fill));
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0x5896D2), &active_border));
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0xA7BAD5), &inactive_border));
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0x202124), &text_brush));
    static_cast<void>(render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.10F), &shadow_brush));

    const RECT surface_bounds = {
        .left = 0,
        .top = 0,
        .right = surface_size_.cx,
        .bottom = surface_size_.cy,
    };
    const HRESULT bind_result = render_target_->BindDC(
        surface_dc_, &surface_bounds);
    if (FAILED(bind_result)) {
        ReleaseDeviceResources();
        EndPaint(hwnd_, &paint);
        return;
    }
    render_target_->BeginDraw();
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    render_target_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    const D2D1_RECT_F viewport = D2D1::RectF(
        static_cast<FLOAT>(layout_.viewport_bounds.left),
        static_cast<FLOAT>(layout_.viewport_bounds.top),
        static_cast<FLOAT>(layout_.viewport_bounds.right),
        static_cast<FLOAT>(layout_.viewport_bounds.bottom));
    render_target_->PushAxisAlignedClip(
        viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const FLOAT scale = static_cast<FLOAT>(dpi_) /
                        USER_DEFAULT_SCREEN_DPI;
    const FLOAT radius = 7.0F * scale;
    const FLOAT border_width = std::max(0.75F, 0.8F * scale);
    const FLOAT edge_inset = std::max(0.5F, border_width / 2.0F);
    const FLOAT icon_size = 16.0F * scale;
    const FLOAT horizontal_padding = 8.0F * scale;
    const FLOAT icon_gap = 6.0F * scale;
    const FLOAT close_half = 3.5F * scale;
    const FLOAT close_stroke = std::max(1.0F, 1.05F * scale);

    for (std::size_t index = 0;
         index < items_.size() && index < layout_.items.size();
         ++index) {
        const TabLayoutItem& layout_item = layout_.items[index];
        const bool active = WindowIdentitiesMatch(
            items_[index].identity, active_identity_);
        const D2D1_RECT_F tab_bounds = D2D1::RectF(
            static_cast<FLOAT>(layout_item.bounds.left) + edge_inset,
            static_cast<FLOAT>(layout_item.bounds.top) + edge_inset,
            static_cast<FLOAT>(layout_item.bounds.right) - edge_inset,
            static_cast<FLOAT>(layout_item.bounds.bottom));
        ID2D1PathGeometry* tab_geometry = nullptr;
        if (SUCCEEDED(CreateTopRoundedGeometry(
                d2d_factory_,
                tab_bounds,
                radius,
                false,
                &tab_geometry))) {
            ID2D1SolidColorBrush* const fill =
                active ? active_fill : inactive_fill;
            if (shadow_brush != nullptr) {
                render_target_->DrawGeometry(
                    tab_geometry,
                    shadow_brush,
                    std::max(1.5F, 2.2F * scale));
            }
            if (fill != nullptr) {
                render_target_->FillGeometry(tab_geometry, fill);
            }
        }
        SafeRelease(&tab_geometry);

        ID2D1PathGeometry* outline_geometry = nullptr;
        if (SUCCEEDED(CreateTopRoundedGeometry(
                d2d_factory_,
                tab_bounds,
                radius,
                true,
                &outline_geometry))) {
            ID2D1SolidColorBrush* const border =
                active ? active_border : inactive_border;
            if (border != nullptr) {
                render_target_->DrawGeometry(
                    outline_geometry, border, border_width);
            }
        }
        SafeRelease(&outline_geometry);

        FLOAT text_left = tab_bounds.left + horizontal_padding;
        HICON icon = reinterpret_cast<HICON>(GetClassLongPtrW(
            items_[index].identity.hwnd, GCLP_HICON));
        if (icon == nullptr) {
            icon = reinterpret_cast<HICON>(GetClassLongPtrW(
                items_[index].identity.hwnd, GCLP_HICONSM));
        }
        ID2D1Bitmap* icon_bitmap = CreateIconBitmap(
            wic_factory_, render_target_, icon);
        if (icon_bitmap != nullptr &&
            tab_bounds.right - tab_bounds.left >=
                icon_size + horizontal_padding * 3.0F) {
            const FLOAT icon_top = tab_bounds.top +
                ((tab_bounds.bottom - tab_bounds.top) - icon_size) / 2.0F;
            render_target_->DrawBitmap(
                icon_bitmap,
                D2D1::RectF(
                    text_left,
                    icon_top,
                    text_left + icon_size,
                    icon_top + icon_size),
                1.0F,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            text_left += icon_size + icon_gap;
        }
        SafeRelease(&icon_bitmap);

        FLOAT text_right = tab_bounds.right - horizontal_padding;
        if (layout_item.close_bounds.right >
            layout_item.close_bounds.left) {
            text_right = static_cast<FLOAT>(
                layout_item.close_bounds.left) - 4.0F * scale;
        }
        const std::wstring& title = items_[index].title;
        const std::wstring_view display_title =
            title.empty() ? std::wstring_view(L"Chrome")
                          : std::wstring_view(title);
        if (text_brush != nullptr && text_right > text_left) {
            render_target_->DrawTextW(
                display_title.data(),
                static_cast<UINT32>(display_title.size()),
                text_format_,
                D2D1::RectF(
                    text_left,
                    tab_bounds.top,
                    text_right,
                    tab_bounds.bottom),
                text_brush,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if (text_brush != nullptr &&
            layout_item.close_bounds.right >
                layout_item.close_bounds.left) {
            const FLOAT center_x =
                (layout_item.close_bounds.left +
                 layout_item.close_bounds.right) /
                2.0F;
            const FLOAT center_y =
                (layout_item.close_bounds.top +
                 layout_item.close_bounds.bottom) /
                2.0F;
            render_target_->DrawLine(
                D2D1::Point2F(
                    center_x - close_half, center_y - close_half),
                D2D1::Point2F(
                    center_x + close_half, center_y + close_half),
                text_brush,
                close_stroke);
            render_target_->DrawLine(
                D2D1::Point2F(
                    center_x + close_half, center_y - close_half),
                D2D1::Point2F(
                    center_x - close_half, center_y + close_half),
                text_brush,
                close_stroke);
        }
    }

    if (text_brush != nullptr && layout_.overflowed &&
        layout_.visible_capacity != 0) {
        const FLOAT inset = 5.0F * scale;
        const FLOAT half_height = 4.0F * scale;
        const FLOAT center_y =
            (viewport.top + viewport.bottom) / 2.0F;
        if (layout_.first_visible_index > 0) {
            const FLOAT x = viewport.left + inset;
            render_target_->DrawLine(
                D2D1::Point2F(x + half_height, center_y - half_height),
                D2D1::Point2F(x, center_y),
                text_brush,
                close_stroke);
            render_target_->DrawLine(
                D2D1::Point2F(x, center_y),
                D2D1::Point2F(x + half_height, center_y + half_height),
                text_brush,
                close_stroke);
        }
        if (layout_.first_visible_index + layout_.visible_capacity <
            items_.size()) {
            const FLOAT x = viewport.right - inset;
            render_target_->DrawLine(
                D2D1::Point2F(x - half_height, center_y - half_height),
                D2D1::Point2F(x, center_y),
                text_brush,
                close_stroke);
            render_target_->DrawLine(
                D2D1::Point2F(x, center_y),
                D2D1::Point2F(x - half_height, center_y + half_height),
                text_brush,
                close_stroke);
        }
    }
    render_target_->PopAxisAlignedClip();
    const HRESULT draw_result = render_target_->EndDraw();
    if (draw_result == D2DERR_RECREATE_TARGET) {
        ReleaseDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (SUCCEEDED(draw_result)) {
        RECT window_bounds{};
        if (GetWindowRect(hwnd_, &window_bounds) != FALSE) {
            POINT destination = {
                .x = window_bounds.left,
                .y = window_bounds.top,
            };
            POINT source{};
            SIZE size = surface_size_;
            BLENDFUNCTION blend = {
                .BlendOp = AC_SRC_OVER,
                .BlendFlags = 0,
                .SourceConstantAlpha = 255,
                .AlphaFormat = AC_SRC_ALPHA,
            };
            static_cast<void>(UpdateLayeredWindow(
                hwnd_,
                nullptr,
                &destination,
                &size,
                surface_dc_,
                &source,
                0,
                &blend,
                ULW_ALPHA));
        }
    }

    SafeRelease(&text_brush);
    SafeRelease(&shadow_brush);
    SafeRelease(&inactive_border);
    SafeRelease(&active_border);
    SafeRelease(&inactive_fill);
    SafeRelease(&active_fill);
    EndPaint(hwnd_, &paint);
}

}  // namespace ctm
