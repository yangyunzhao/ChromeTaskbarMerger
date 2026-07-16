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
    L"ChromeTaskbarMerger.V2.Phase1.IntegrationTest";
constexpr wchar_t kTabStripWindowClass[] =
    L"ChromeTaskbarMerger.V2.Phase1.TabStrip";

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
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    ScopedTestWindow() = default;
    ScopedTestWindow(const ScopedTestWindow&) = delete;
    ScopedTestWindow& operator=(const ScopedTestWindow&) = delete;

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
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
                   "Phase 1 close hit testing must not close a real window automatically");
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

    Expect(FindWindowW(kTestWindowClass, nullptr) == nullptr,
           "the integration test should leave no synthetic group windows");
    Expect(FindWindowW(kTabStripWindowClass, nullptr) == nullptr,
           "the integration test should leave no native tab-strip window");
}

}  // namespace

int main() {
    TestGeometryPolicy();
    TestSyntheticWindowGroupingSwitchAndRestore();

    if (failures != 0) {
        std::cerr << failures << " window-group integration test(s) failed.\n";
        return 1;
    }
    std::cout << "All window-group integration tests passed.\n";
    return 0;
}
