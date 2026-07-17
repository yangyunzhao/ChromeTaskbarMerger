#include "tab_activation.h"
#include "tab_group_model.h"
#include "tab_strip_window.h"
#include "window_coordinator.h"
#include "window_identity_query.h"

#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kTestWindowClass[] =
    L"ChromeTaskbarMerger.V2.Phase2.IntegrationTest";
constexpr wchar_t kTabStripWindowClass[] =
    L"ChromeTaskbarMerger.V2.TabStrip";

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] bool RectanglesEqual(const RECT& left,
                                   const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] bool WindowIsAbove(const HWND upper,
                                 const HWND lower) noexcept {
    for (HWND current = upper; current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (current == lower) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CurrentProcessOwnsWindowOfClass(
    const wchar_t* const class_name) noexcept {
    HWND window = nullptr;
    while ((window = FindWindowExW(
                nullptr, window, class_name, nullptr)) != nullptr) {
        DWORD process_id = 0;
        static_cast<void>(GetWindowThreadProcessId(window, &process_id));
        if (process_id == GetCurrentProcessId()) {
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK TestWindowProcedure(const HWND window,
                                     const UINT message,
                                     const WPARAM wparam,
                                     const LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

class ScopedTestWindowClass final {
public:
    [[nodiscard]] bool Register() {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = TestWindowProcedure;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kTestWindowClass;
        return RegisterClassExW(&window_class) != 0;
    }

    ~ScopedTestWindowClass() {
        if (instance_ != nullptr) {
            UnregisterClassW(kTestWindowClass, instance_);
        }
    }

    ScopedTestWindowClass() = default;
    ScopedTestWindowClass(const ScopedTestWindowClass&) = delete;
    ScopedTestWindowClass& operator=(const ScopedTestWindowClass&) = delete;

private:
    HINSTANCE instance_ = nullptr;
};

class ScopedTestWindow final {
public:
    [[nodiscard]] bool Create(const wchar_t* const title,
                              const RECT& bounds) {
        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kTestWindowClass,
            title,
            WS_OVERLAPPEDWINDOW,
            bounds.left,
            bounds.top,
            bounds.right - bounds.left,
            bounds.bottom - bounds.top,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (hwnd_ == nullptr) {
            return false;
        }
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        static_cast<void>(UpdateWindow(hwnd_));
        return true;
    }

    ~ScopedTestWindow() {
        Destroy();
    }

    ScopedTestWindow() = default;
    ScopedTestWindow(const ScopedTestWindow&) = delete;
    ScopedTestWindow& operator=(const ScopedTestWindow&) = delete;

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }

    void Destroy() noexcept {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

private:
    HWND hwnd_ = nullptr;
};

class CoordinatingTabSink final : public ctm::ITabStripEventSink {
public:
    CoordinatingTabSink(ctm::TabActivationCoordinator* const activation,
                        ctm::TabStripWindow* const strip)
        : activation_(activation), strip_(strip) {}

    void OnTabActivationRequested(
        const ctm::WindowIdentity& identity) override {
        ++activation_requests;
        last_activation = activation_->Activate(identity);
        if (!last_activation.succeeded) {
            return;
        }
        DWORD owner_error = ERROR_SUCCESS;
        owner_updated = strip_->SetOwner(identity.hwnd, &owner_error);
        active_updated = strip_->SetActive(identity);
    }

    void OnTabCloseRequested(
        const ctm::WindowIdentity& identity) override {
        ++close_requests;
        last_close_identity = identity;
    }

    int activation_requests = 0;
    int close_requests = 0;
    bool owner_updated = false;
    bool active_updated = false;
    ctm::WindowIdentity last_close_identity;
    ctm::TabActivationReport last_activation;

private:
    ctm::TabActivationCoordinator* activation_ = nullptr;
    ctm::TabStripWindow* strip_ = nullptr;
};

class DeterministicTestActivationGateway final
    : public ctm::IWindowActivationGateway {
public:
    [[nodiscard]] ctm::WindowActivationResult Verify(
        const ctm::WindowIdentity& identity) override {
        const ctm::WindowIdentityQueryResult current =
            ctm::QueryWindowIdentity(identity.hwnd);
        if (!current.succeeded ||
            !ctm::WindowIdentitiesMatch(identity, current.identity)) {
            return {
                .win32_error = ERROR_INVALID_WINDOW_HANDLE,
                .message = L"The deterministic target identity is stale.",
            };
        }
        return {
            .succeeded = true,
            .message = L"The deterministic target identity is current.",
        };
    }

    [[nodiscard]] ctm::WindowActivationResult Activate(
        const ctm::WindowIdentity& identity) override {
        ctm::WindowActivationResult result = Verify(identity);
        if (!result.succeeded) {
            return result;
        }
        activation_calls.push_back(identity);
        if (SetWindowPos(
                identity.hwnd,
                HWND_TOP,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE) {
            return {
                .win32_error = GetLastError(),
                .message = L"The deterministic Z-order change failed.",
            };
        }
        return {
            .succeeded = true,
            .message = L"The deterministic target was raised.",
        };
    }

    std::vector<ctm::WindowIdentity> activation_calls;
};

void TestGeometryPolicy() {
    const RECT group = {.left = 100, .top = 120, .right = 900, .bottom = 720};
    const ctm::WindowGroupGeometry geometry =
        ctm::CalculateWindowGroupGeometry(group, ctm::kV2TabStripHeight);
    Expect(geometry.valid,
           "a normal desktop rectangle should produce valid group geometry");
    Expect(RectanglesEqual(geometry.tab_strip_bounds,
                           {.left = 100,
                            .top = 120,
                            .right = 900,
                            .bottom = 158}),
           "the tab strip should occupy the top of the group rectangle");
    Expect(RectanglesEqual(geometry.content_bounds,
                           {.left = 100,
                            .top = 158,
                            .right = 900,
                            .bottom = 720}),
           "the shared Chrome rectangle should begin below the tab strip");
    Expect(!ctm::CalculateWindowGroupGeometry(
                {.left = 0, .top = 0, .right = 100, .bottom = 100},
                ctm::kV2TabStripHeight)
                .valid,
           "unsafe tiny geometry should be rejected");

    const RECT work_area = {
        .left = -1920,
        .top = 0,
        .right = 0,
        .bottom = 1040,
    };
    const ctm::WindowGroupGeometry moved =
        ctm::CalculateWindowGroupGeometryFromContentBounds(
            {.left = -1700, .top = 238, .right = -900, .bottom = 838},
            work_area,
            ctm::kV2TabStripHeight);
    Expect(moved.valid &&
               RectanglesEqual(
                   moved.group_bounds,
                   {.left = -1700,
                    .top = 200,
                    .right = -900,
                    .bottom = 838}) &&
               RectanglesEqual(
                   moved.content_bounds,
                   {.left = -1700,
                    .top = 238,
                    .right = -900,
                    .bottom = 838}),
           "a moved content rectangle should reconstruct its group on a negative-coordinate monitor");

    const ctm::WindowGroupGeometry snapped =
        ctm::CalculateWindowGroupGeometryFromContentBounds(
            {.left = -1920, .top = 0, .right = -960, .bottom = 1040},
            work_area,
            ctm::kV2TabStripHeight);
    Expect(snapped.valid &&
               ctm::RectangleFitsWithin(snapped.group_bounds, work_area) &&
               snapped.tab_strip_bounds.top == work_area.top &&
               snapped.content_bounds.top ==
                   work_area.top + ctm::kV2TabStripHeight,
           "a snapped content rectangle should leave visible room for the external strip");

    const ctm::WindowGroupGeometry boundary_group =
        ctm::CalculateWindowGroupGeometry(
            {.left = -1920,
             .top = 0,
             .right = 0,
             .bottom = 1040},
            ctm::kV2TabStripHeight);
    const RECT vertically_displaced_driver = {
        .left = -1920,
        .top = 78,
        .right = 0,
        .bottom = 1080,
    };
    const ctm::WindowGroupGeometry clamped_to_same_boundary =
        ctm::CalculateWindowGroupGeometryFromContentBounds(
            vertically_displaced_driver,
            work_area,
            ctm::kV2TabStripHeight);
    Expect(
        boundary_group.valid && clamped_to_same_boundary.valid &&
            RectanglesEqual(
                boundary_group.group_bounds,
                clamped_to_same_boundary.group_bounds) &&
            ctm::WindowGroupArrangementRequired(
                boundary_group,
                clamped_to_same_boundary,
                vertically_displaced_driver),
        "a vertically dragged member must be corrected even when work-area clamping leaves the group rectangle unchanged");
    Expect(
        !ctm::WindowGroupArrangementRequired(
            boundary_group,
            clamped_to_same_boundary,
            clamped_to_same_boundary.content_bounds),
        "an already aligned driver should not cause a geometry feedback loop at the work-area boundary");

    Expect(ctm::ScalePixelsForDpi(38, 96) == 38 &&
               ctm::ScalePixelsForDpi(38, 120) == 48 &&
               ctm::ScalePixelsForDpi(38, 144) == 57 &&
               ctm::ScalePixelsForDpi(38, 192) == 76,
           "tab-strip metrics should scale at 100, 125, 150, and 200 percent DPI");
    Expect(ctm::IsFullscreenRectangle(
               {.left = -1920, .top = 0, .right = 0, .bottom = 1080},
               {.left = -1920, .top = 0, .right = 0, .bottom = 1080},
               1) &&
               !ctm::IsFullscreenRectangle(
                   {.left = -1920, .top = 0, .right = 0, .bottom = 1040},
                   {.left = -1920, .top = 0, .right = 0, .bottom = 1080},
                   1),
           "fullscreen detection should distinguish monitor bounds from its work area");
}

void TestSyntheticWindowGroupingSwitchAndRestore() {
    ScopedTestWindowClass window_class;
    Expect(window_class.Register(),
           "the synthetic integration window class should register");
    if (failures != 0) {
        return;
    }

    {
        constexpr std::array<RECT, 3> original_bounds = {{
            {.left = 80, .top = 100, .right = 600, .bottom = 500},
            {.left = 140, .top = 150, .right = 660, .bottom = 550},
            {.left = 200, .top = 200, .right = 720, .bottom = 600},
        }};
        std::array<ScopedTestWindow, 3> windows;
        const std::array<const wchar_t*, 3> titles = {
            L"Synthetic Chrome One",
            L"Synthetic Chrome Two",
            L"Synthetic Chrome Three",
        };
        bool all_created = true;
        for (std::size_t index = 0; index < windows.size(); ++index) {
            all_created = windows[index].Create(
                              titles[index], original_bounds[index]) &&
                          all_created;
        }
        Expect(all_created,
               "three temporary top-level windows should be created");
        if (!all_created) {
            return;
        }

        std::vector<ctm::WindowIdentity> identities;
        std::vector<ctm::TabGroupCandidate> candidates;
        std::vector<ctm::TabStripItem> strip_items;
        for (std::size_t index = 0; index < windows.size(); ++index) {
            const ctm::WindowIdentityQueryResult query =
                ctm::QueryWindowIdentity(windows[index].hwnd());
            Expect(query.succeeded,
                   "each temporary window should have a complete identity");
            identities.push_back(query.identity);
            candidates.push_back({
                .identity = query.identity,
                .title = titles[index],
            });
            strip_items.push_back({
                .identity = query.identity,
                .title = titles[index],
            });
        }

        ctm::TabGroupModel model;
        static_cast<void>(model.Synchronize(candidates, identities.front()));
        for (const ctm::WindowIdentity& identity : identities) {
            Expect(model.MarkTabCreated(identity, true),
                   "every synthetic internal tab should be marked ready");
        }

        ctm::WindowGroupPlacementController placement;
        const ctm::WindowCoordinationResult capture =
            placement.Capture(identities);
        Expect(capture.succeeded,
               "the original synthetic layouts should be captured");
        const ctm::WindowGroupGeometry geometry =
            ctm::CalculateWindowGroupGeometry(
                original_bounds.front(), ctm::kV2TabStripHeight);
        const ctm::WindowCoordinationResult arranged =
            placement.Arrange(geometry, identities.front());
        Expect(arranged.succeeded && placement.needs_restore(),
               "temporary windows should arrange into a restorable group");
        for (const ScopedTestWindow& window : windows) {
            RECT current{};
            GetWindowRect(window.hwnd(), &current);
            Expect(RectanglesEqual(current, geometry.content_bounds),
                   "all temporary windows should share the content rectangle");
        }
        Expect(WindowIsAbove(identities.front().hwnd, identities[1].hwnd) &&
                   WindowIsAbove(identities.front().hwnd, identities[2].hwnd),
               "the initial active member should be above its peers");

        ctm::Win32WindowActivationGateway real_gateway;
        for (const ctm::WindowIdentity& identity : identities) {
            Expect(real_gateway.Verify(identity).succeeded,
                   "the real Win32 gateway should verify each current temporary identity");
        }
        ctm::WindowIdentity invalid_real_identity = identities.front();
        ++invalid_real_identity.process_id;
        Expect(!real_gateway.Verify(invalid_real_identity).succeeded,
               "the real Win32 gateway should reject a stale temporary identity");

        DeterministicTestActivationGateway gateway;
        ctm::TabActivationCoordinator activation(&model, &gateway);
        ctm::TabStripWindow strip;
        CoordinatingTabSink sink(&activation, &strip);
        DWORD strip_error = ERROR_SUCCESS;
        const bool strip_created = strip.Create(
            GetModuleHandleW(nullptr),
            identities.front().hwnd,
            geometry.tab_strip_bounds,
            strip_items,
            identities.front(),
            &sink,
            &strip_error);
        Expect(strip_created && strip.IsHealthy(),
               "the temporary native tab strip should be healthy");
        model.SetTabStripHealthy(strip.IsHealthy());
        for (const ctm::WindowIdentity& identity : identities) {
            const ctm::TabActivationReport verified =
                activation.Verify(identity);
            Expect(verified.succeeded,
                   "each synthetic tab activation path should verify");
        }

        static_cast<void>(SetForegroundWindow(identities.front().hwnd));
        const ctm::TabStripLayout& layout = strip.layout();
        Expect(layout.items.size() == identities.size(),
               "the real strip window should expose three hit regions");
        if (layout.items.size() == identities.size()) {
            const RECT& second_bounds = layout.items[1].bounds;
            const LPARAM second_point = MAKELPARAM(
                second_bounds.left + 5,
                second_bounds.top +
                    (second_bounds.bottom - second_bounds.top) / 2);
            SendMessageW(strip.hwnd(), WM_LBUTTONUP, 0, second_point);
            Expect(sink.activation_requests == 1,
                   "a synthetic tab-body click should request exactly one activation");
            Expect(sink.last_activation.gateway_called,
                   "the verified tab click should reach the Win32 activation gateway");
            Expect(sink.last_activation.succeeded,
                   "the deterministic activation gateway should accept the synthetic target");
            if (!sink.last_activation.succeeded) {
                std::wcerr << L"Activation error "
                           << sink.last_activation.win32_error << L": "
                           << sink.last_activation.message << L'\n';
            }
            Expect(sink.owner_updated && sink.active_updated,
                   "a successful tab click should update strip ownership and active paint state");
            Expect(model.active_identity().has_value() &&
                       ctm::WindowIdentitiesMatch(
                           *model.active_identity(), identities[1]),
                   "the clicked synthetic member should become active in the group model");
            Expect(gateway.activation_calls.size() == 1 &&
                       ctm::WindowIdentitiesMatch(
                           gateway.activation_calls.front(), identities[1]),
                   "the click should send only the exact second identity to the activation gateway");
            Expect(GetWindow(strip.hwnd(), GW_OWNER) == identities[1].hwnd,
                   "the tab-strip owner should follow the active member");
            Expect(WindowIsAbove(identities[1].hwnd, identities[0].hwnd) &&
                       WindowIsAbove(identities[1].hwnd, identities[2].hwnd),
                   "the clicked member should become the top group window");

            const RECT& close_bounds = layout.items[2].close_bounds;
            const LPARAM close_point = MAKELPARAM(
                close_bounds.left +
                    (close_bounds.right - close_bounds.left) / 2,
                close_bounds.top +
                    (close_bounds.bottom - close_bounds.top) / 2);
            SendMessageW(strip.hwnd(), WM_LBUTTONUP, 0, close_point);
            Expect(sink.close_requests == 1 &&
                       ctm::WindowIdentitiesMatch(
                           sink.last_close_identity, identities[2]),
                   "the close hit region should report only its exact identity");
            Expect(IsWindow(identities[2].hwnd) != FALSE,
                   "the strip should delegate closing to its lifecycle event sink");
        }

        const RECT moved_content = {
            .left = 260,
            .top = 278,
            .right = 860,
            .bottom = 678,
        };
        Expect(SetWindowPos(
                   identities[1].hwnd,
                   nullptr,
                   moved_content.left,
                   moved_content.top,
                   moved_content.right - moved_content.left,
                   moved_content.bottom - moved_content.top,
                   SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
               "one synthetic member should accept an interactive move and resize");
        const RECT work_area = {
            .left = 0,
            .top = 0,
            .right = 1920,
            .bottom = 1040,
        };
        const ctm::WindowGroupGeometry moved_geometry =
            ctm::CalculateWindowGroupGeometryFromContentBounds(
                moved_content, work_area, ctm::kV2TabStripHeight);
        Expect(moved_geometry.valid &&
                   placement.Arrange(moved_geometry, identities[1]).succeeded,
               "the moved member should drive a new shared group rectangle");
        DWORD bounds_error = ERROR_SUCCESS;
        Expect(strip.SetBounds(
                   moved_geometry.tab_strip_bounds, &bounds_error),
               "the native strip should follow the moved group rectangle");
        for (const ScopedTestWindow& window : windows) {
            RECT current{};
            GetWindowRect(window.hwnd(), &current);
            Expect(RectanglesEqual(current, moved_geometry.content_bounds),
                   "all group members should follow the moved and resized driver");
        }
        RECT strip_bounds{};
        GetWindowRect(strip.hwnd(), &strip_bounds);
        Expect(RectanglesEqual(
                   strip_bounds, moved_geometry.tab_strip_bounds),
               "the external tab strip should remain attached above the moved group");

        for (const ctm::WindowIdentity& identity : identities) {
            ShowWindow(identity.hwnd, SW_MINIMIZE);
        }
        Expect(IsIconic(identities[0].hwnd) != FALSE &&
                   IsIconic(identities[1].hwnd) != FALSE &&
                   IsIconic(identities[2].hwnd) != FALSE,
               "a group minimize transaction should minimize every member");
        DWORD visibility_error = ERROR_SUCCESS;
        Expect(strip.SetVisible(false, &visibility_error) &&
                   IsWindowVisible(strip.hwnd()) == FALSE,
               "the strip should hide while the group is minimized or fullscreen");

        for (const ctm::WindowIdentity& identity : identities) {
            ShowWindow(identity.hwnd, SW_RESTORE);
        }
        Expect(placement.ArrangeAsNormal(
                   moved_geometry, identities[1]).succeeded &&
                   strip.SetVisible(true, &visibility_error),
               "restoring the group should reapply its shared normal geometry and strip");
        for (const ctm::WindowIdentity& identity : identities) {
            Expect(IsIconic(identity.hwnd) == FALSE,
                   "every minimized member should become normal again");
        }

        const ctm::WindowGroupGeometry maximized_geometry =
            ctm::CalculateWindowGroupGeometry(
                work_area, ctm::kV2TabStripHeight);
        for (const ctm::WindowIdentity& identity : identities) {
            ShowWindow(identity.hwnd, SW_MAXIMIZE);
        }
        Expect(placement.ArrangeAsNormal(
                   maximized_geometry, identities[1]).succeeded &&
                   strip.SetBounds(
                       maximized_geometry.tab_strip_bounds, &bounds_error),
               "managed maximize should normalize native state and reserve the monitor top for its external strip");
        for (const ctm::WindowIdentity& identity : identities) {
            RECT current{};
            GetWindowRect(identity.hwnd, &current);
            const LONG_PTR style = GetWindowLongPtrW(
                identity.hwnd, GWL_STYLE);
            Expect(IsZoomed(identity.hwnd) == FALSE &&
                       RectanglesEqual(
                           current, maximized_geometry.content_bounds),
                   "each managed-maximized member should be native-normal below the external strip");
            Expect((style & WS_CAPTION) != 0 &&
                       (style & WS_SYSMENU) != 0 &&
                       (style & WS_MINIMIZEBOX) != 0 &&
                       (style & WS_MAXIMIZEBOX) != 0,
                   "managed maximize should preserve the native caption and all system buttons");
        }

        for (const ctm::WindowIdentity& identity : identities) {
            ShowWindow(identity.hwnd, SW_MINIMIZE);
        }
        Expect(placement.ArrangeAsNormal(
                   maximized_geometry, identities[1]).succeeded,
               "restoring a minimized managed-maximized group should reapply its constrained geometry");
        for (const ctm::WindowIdentity& identity : identities) {
            RECT current{};
            GetWindowRect(identity.hwnd, &current);
            Expect(IsIconic(identity.hwnd) == FALSE &&
                       IsZoomed(identity.hwnd) == FALSE &&
                       RectanglesEqual(
                           current, maximized_geometry.content_bounds),
                   "every minimized managed-maximized member should restore below the external strip");
        }

        ShowWindow(identities[1].hwnd, SW_MAXIMIZE);
        Expect(IsZoomed(identities[1].hwnd) != FALSE,
               "a second native maximize request should be observable as the managed restore gesture");
        Expect(placement.ArrangeAsNormal(
                   moved_geometry, identities[1]).succeeded &&
                   strip.SetBounds(
                       moved_geometry.tab_strip_bounds, &bounds_error),
               "the managed restore gesture should recover the prior normal group rectangle");
        for (const ctm::WindowIdentity& identity : identities) {
            RECT current{};
            GetWindowRect(identity.hwnd, &current);
            Expect(IsZoomed(identity.hwnd) == FALSE &&
                       RectanglesEqual(current, moved_geometry.content_bounds),
                   "every member should return to native normal state and the prior rectangle");
        }

        ctm::WindowIdentity stale_identity = identities[1];
        ++stale_identity.process_creation_time;
        const ctm::TabActivationReport stale =
            activation.Activate(stale_identity);
        Expect(!stale.succeeded && !stale.gateway_called,
               "a stale identity should be rejected before any Win32 activation");

        const ctm::WindowGroupRestoreReport restored = placement.RestoreAll();
        Expect(restored.succeeded && !placement.needs_restore() &&
                   restored.restored_count == windows.size(),
               "all temporary layouts should restore successfully");
        for (std::size_t index = 0; index < windows.size(); ++index) {
            RECT current{};
            GetWindowRect(windows[index].hwnd(), &current);
            Expect(RectanglesEqual(current, original_bounds[index]),
                   "each temporary window should regain its exact original rectangle");
        }
        strip.Destroy();
    }

    Expect(!CurrentProcessOwnsWindowOfClass(kTestWindowClass),
           "the integration test should leave no synthetic group windows");
    Expect(!CurrentProcessOwnsWindowOfClass(kTabStripWindowClass),
           "the integration test should leave no native tab-strip window");
}

void TestDynamicParticipantAddCloseAndRestore() {
    ScopedTestWindowClass window_class;
    Expect(window_class.Register(),
           "the dynamic integration window class should register");
    if (failures != 0) {
        return;
    }

    constexpr std::array<RECT, 3> original_bounds = {{
        {.left = 60, .top = 80, .right = 500, .bottom = 420},
        {.left = 120, .top = 140, .right = 560, .bottom = 480},
        {.left = 180, .top = 200, .right = 620, .bottom = 540},
    }};
    std::array<ScopedTestWindow, 3> windows;
    Expect(windows[0].Create(L"Dynamic One", original_bounds[0]) &&
               windows[1].Create(L"Dynamic Two", original_bounds[1]),
           "two initial dynamic windows should be created");
    if (windows[0].hwnd() == nullptr || windows[1].hwnd() == nullptr) {
        return;
    }

    std::vector<ctm::WindowIdentity> identities;
    for (std::size_t index = 0; index < 2; ++index) {
        const ctm::WindowIdentityQueryResult query =
            ctm::QueryWindowIdentity(windows[index].hwnd());
        Expect(query.succeeded,
               "an initial dynamic window identity should be queryable");
        identities.push_back(query.identity);
    }

    ctm::WindowGroupPlacementController placement;
    Expect(placement.Capture(identities).succeeded,
           "initial dynamic placements should be captured");
    const ctm::WindowGroupGeometry geometry =
        ctm::CalculateWindowGroupGeometry(
            original_bounds.front(), ctm::kV2TabStripHeight);
    Expect(placement.Arrange(geometry, identities.front()).succeeded,
           "the initial dynamic group should arrange");

    Expect(windows[2].Create(L"Dynamic Three", original_bounds[2]),
           "a third window should be created after management starts");
    if (windows[2].hwnd() == nullptr) {
        return;
    }
    const ctm::WindowIdentityQueryResult added =
        ctm::QueryWindowIdentity(windows[2].hwnd());
    Expect(added.succeeded,
           "the new dynamic member identity should be queryable");
    identities.push_back(added.identity);
    const ctm::WindowCoordinationResult participants =
        placement.SynchronizeParticipants(identities);
    Expect(participants.succeeded &&
               placement.captured_window_count() == 3,
           "the new member should receive one original-layout record");
    Expect(placement.Arrange(geometry, identities.back()).succeeded,
           "the expanded dynamic group should arrange with the new member active");
    for (const ScopedTestWindow& window : windows) {
        RECT current{};
        GetWindowRect(window.hwnd(), &current);
        Expect(RectanglesEqual(current, geometry.content_bounds),
               "every expanded dynamic member should share the group rectangle");
    }

    windows[1].Destroy();
    const std::array remaining = {identities[0], identities[2]};
    Expect(placement.SynchronizeParticipants(remaining).succeeded,
           "closing a member should remove it from current arrangement work");
    Expect(placement.Arrange(geometry, identities[2]).succeeded,
           "remaining members should still arrange after a close");

    const ctm::WindowGroupRestoreReport restored = placement.RestoreAll();
    Expect(restored.succeeded && !placement.needs_restore() &&
               restored.restored_count == 2 &&
               restored.safely_skipped_count == 1,
           "live dynamic members should restore and the closed identity should be safely skipped");
    for (const std::size_t index : {std::size_t{0}, std::size_t{2}}) {
        RECT current{};
        GetWindowRect(windows[index].hwnd(), &current);
        Expect(RectanglesEqual(current, original_bounds[index]),
               "each live dynamic member should regain its own original rectangle");
    }
}

}  // namespace

int main() {
    TestGeometryPolicy();
    TestSyntheticWindowGroupingSwitchAndRestore();
    TestDynamicParticipantAddCloseAndRestore();

    if (failures != 0) {
        std::cerr << failures << " window-group integration test(s) failed.\n";
        return 1;
    }
    std::cout << "All window-group integration tests passed.\n";
    return 0;
}
