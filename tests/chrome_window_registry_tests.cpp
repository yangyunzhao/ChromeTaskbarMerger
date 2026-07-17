#include "chrome_window_registry.h"

#include <Windows.h>

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kTestWindowClass[] =
    L"ChromeTaskbarMerger.V2.Phase2.RegistryTest";

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] HWND TestHandle(const std::uintptr_t value) noexcept {
    return reinterpret_cast<HWND>(value);
}

[[nodiscard]] ctm::ChromeWindowSnapshot MakeSnapshot(
    const std::uintptr_t handle,
    const std::wstring_view title,
    const DWORD process_id = 100,
    const DWORD thread_id = 200,
    const std::uint64_t creation_time = 300) {
    ctm::ChromeWindowSnapshot snapshot;
    snapshot.hwnd = TestHandle(handle);
    snapshot.process_id = process_id;
    snapshot.thread_id = thread_id;
    snapshot.process_creation_time = creation_time;
    snapshot.process_path = L"C:\\Chrome\\chrome.exe";
    snapshot.title = title;
    snapshot.class_name = L"Chrome_WidgetWin_1";
    snapshot.process_path_available = true;
    snapshot.process_creation_time_available = true;
    snapshot.class_name_available = true;
    snapshot.style_available = true;
    snapshot.extended_style_available = true;
    snapshot.top_level = true;
    snapshot.visible = true;
    return snapshot;
}

[[nodiscard]] std::uintptr_t ActiveHandle(
    const ctm::ChromeWindowRegistry& registry) noexcept {
    return registry.active_identity().has_value()
               ? reinterpret_cast<std::uintptr_t>(
                     registry.active_identity()->hwnd)
               : 0;
}

void TestCreateCloseReopenAndActiveFallback() {
    ctm::ChromeWindowRegistry registry;
    const std::vector initial = {
        MakeSnapshot(1, L"One"),
        MakeSnapshot(2, L"Two"),
        MakeSnapshot(3, L"Three"),
    };
    const ctm::ChromeWindowRegistryReport created =
        registry.Synchronize(initial, TestHandle(2));
    Expect(created.succeeded && created.added_count == 3 &&
               registry.windows().size() == 3,
           "three initial windows should be registered");
    Expect(ActiveHandle(registry) == 2,
           "the foreground member should become active");

    const std::vector without_non_active = {
        MakeSnapshot(1, L"One"), MakeSnapshot(2, L"Two")};
    const ctm::ChromeWindowRegistryReport closed_non_active =
        registry.Synchronize(without_non_active, TestHandle(2));
    Expect(closed_non_active.removed_count == 1 &&
               !closed_non_active.active_changed &&
               ActiveHandle(registry) == 2,
           "closing a non-active member should retain the active member");

    const std::vector without_active = {MakeSnapshot(1, L"One")};
    const ctm::ChromeWindowRegistryReport closed_active =
        registry.Synchronize(without_active);
    Expect(closed_active.removed_count == 1 &&
               closed_active.active_changed &&
               ActiveHandle(registry) == 1,
           "closing the active member should choose a remaining fallback");

    const ctm::ChromeWindowRegistryReport closed_all =
        registry.Synchronize({});
    Expect(closed_all.removed_count == 1 && registry.windows().empty() &&
               !registry.active_identity().has_value(),
           "closing all windows should empty the registry");

    const ctm::ChromeWindowSnapshot reopened =
        MakeSnapshot(4, L"Reopened", 101, 201, 301);
    const ctm::ChromeWindowRegistryReport reopen_report =
        registry.Synchronize(std::span(&reopened, 1), reopened.hwnd);
    Expect(reopen_report.added_count == 1 && ActiveHandle(registry) == 4,
           "a window reopened after an empty group should be added and active");
}

void TestTitleStormAndNoChangeAreIdempotent() {
    ctm::ChromeWindowRegistry registry;
    ctm::ChromeWindowSnapshot snapshot = MakeSnapshot(10, L"Original");
    static_cast<void>(registry.Synchronize(std::span(&snapshot, 1)));

    const ctm::ChromeWindowRegistryReport unchanged =
        registry.Synchronize(std::span(&snapshot, 1));
    Expect(!unchanged.HasVisualChanges(),
           "an identical full scan should not report visual work");

    snapshot.title = L"Updated";
    const ctm::ChromeWindowRegistryReport updated =
        registry.Synchronize(std::span(&snapshot, 1));
    Expect(updated.updated_title_count == 1 &&
               !updated.HasMembershipChanges() &&
               registry.windows().front().title == L"Updated",
           "a title-only update should not look like a lifecycle replacement");
}

void TestIdentityReplacementAndDestroyHintResetSameIdentity() {
    ctm::ChromeWindowRegistry registry;
    const ctm::ChromeWindowSnapshot original = MakeSnapshot(20, L"Original");
    static_cast<void>(registry.Synchronize(std::span(&original, 1)));

    const ctm::ChromeWindowSnapshot different_identity =
        MakeSnapshot(20, L"Replacement", 101, 201, 301);
    const ctm::ChromeWindowRegistryReport replaced =
        registry.Synchronize(std::span(&different_identity, 1));
    Expect(replaced.replaced_identity_count == 1 &&
               replaced.removed_count == 1 && replaced.added_count == 1,
           "a reused HWND with different OS identity should be replaced");

    const HWND destroyed = different_identity.hwnd;
    const ctm::ChromeWindowRegistryReport invalidated =
        registry.InvalidateHandles(std::span(&destroyed, 1));
    Expect(invalidated.removed_count == 1 && registry.windows().empty(),
           "a destroy hint should invalidate even an otherwise equal identity");
    const ctm::ChromeWindowRegistryReport same_identity_reopened =
        registry.Synchronize(std::span(&different_identity, 1));
    Expect(same_identity_reopened.added_count == 1 &&
               registry.windows().size() == 1,
           "a post-destroy HWND reuse must receive fresh registry state");
}

void TestManagedWindowLimitRetainsTheExistingGroup() {
    const std::vector candidates = {
        MakeSnapshot(1, L"One"),
        MakeSnapshot(2, L"Two"),
        MakeSnapshot(3, L"Three"),
        MakeSnapshot(4, L"Four"),
        MakeSnapshot(5, L"Five"),
        MakeSnapshot(6, L"Six"),
    };
    const std::vector current(
        candidates.begin(), candidates.begin() + 5);
    const ctm::ManagedChromeWindowSelection full =
        ctm::SelectManagedChromeWindows(
            candidates, current, TestHandle(6), 5);
    Expect(full.selected.size() == 5 && full.overflow.size() == 1,
           "a sixth Chrome window should be isolated without dropping the five-member group");
    if (full.selected.size() == 5 && full.overflow.size() == 1) {
        for (std::size_t index = 0; index < full.selected.size(); ++index) {
            Expect(full.selected[index].hwnd == current[index].hwnd,
                   "the existing group order should remain stable at the management limit");
        }
        Expect(full.overflow.front().hwnd == TestHandle(6),
               "the new foreground overflow window should keep its independent taskbar entry");
    }

    const std::vector partial(
        candidates.begin(), candidates.begin() + 3);
    const ctm::ManagedChromeWindowSelection filled =
        ctm::SelectManagedChromeWindows(
            candidates, partial, TestHandle(6), 5);
    Expect(filled.selected.size() == 5 && filled.overflow.size() == 1 &&
               filled.selected[0].hwnd == TestHandle(1) &&
               filled.selected[1].hwnd == TestHandle(2) &&
               filled.selected[2].hwnd == TestHandle(3) &&
               filled.selected[3].hwnd == TestHandle(6) &&
               filled.selected[4].hwnd == TestHandle(4) &&
               filled.overflow[0].hwnd == TestHandle(5),
           "free slots should retain current members, then prefer a new foreground window deterministically");

    const ctm::ManagedChromeWindowSelection none =
        ctm::SelectManagedChromeWindows(candidates, {}, nullptr, 0);
    Expect(none.selected.empty() &&
               none.overflow.size() == candidates.size(),
           "a zero limit should safely leave every candidate unmanaged");
}

[[nodiscard]] LRESULT CALLBACK TestWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

[[nodiscard]] ctm::ChromeWindowSnapshot SnapshotForRealWindow(
    const HWND hwnd) {
    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(hwnd, &process_id);
    wchar_t title[128]{};
    static_cast<void>(GetWindowTextW(hwnd, title, 128));
    ctm::ChromeWindowSnapshot snapshot =
        MakeSnapshot(
            reinterpret_cast<std::uintptr_t>(hwnd),
            title,
            process_id,
            thread_id,
            1);
    snapshot.class_name = kTestWindowClass;
    return snapshot;
}

void TestTemporaryTopLevelTitleDestroyAndMainThreadSerialization() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &TestWindowProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = kTestWindowClass;
    const ATOM atom = RegisterClassExW(&window_class);
    Expect(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
           "the registry integration window class should register");

    const HWND first = CreateWindowExW(
        0, kTestWindowClass, L"First title", WS_OVERLAPPEDWINDOW,
        10, 10, 320, 240, nullptr, nullptr, instance, nullptr);
    const HWND second = CreateWindowExW(
        0, kTestWindowClass, L"Second title", WS_OVERLAPPEDWINDOW,
        40, 40, 320, 240, nullptr, nullptr, instance, nullptr);
    Expect(first != nullptr && second != nullptr,
           "two temporary top-level windows should be created");
    if (first == nullptr || second == nullptr) {
        if (first != nullptr) {
            DestroyWindow(first);
        }
        if (second != nullptr) {
            DestroyWindow(second);
        }
        return;
    }

    ctm::ChromeWindowRegistry registry;
    std::vector snapshots = {
        SnapshotForRealWindow(first), SnapshotForRealWindow(second)};
    const ctm::ChromeWindowRegistryReport initial =
        registry.Synchronize(snapshots, second);
    Expect(initial.added_count == 2 && registry.windows().size() == 2,
           "temporary windows should enter the registry on its owner thread");

    SetWindowTextW(first, L"First title updated");
    snapshots[0] = SnapshotForRealWindow(first);
    const ctm::ChromeWindowRegistryReport title =
        registry.Synchronize(snapshots, first);
    Expect(title.updated_title_count == 1 && title.active_changed,
           "a real title and foreground hint update should serialize together");

    ctm::ChromeWindowRegistryReport wrong_thread;
    std::thread worker([&registry, &snapshots, &wrong_thread]() {
        wrong_thread = registry.Synchronize(snapshots);
    });
    worker.join();
    Expect(!wrong_thread.succeeded &&
               wrong_thread.win32_error == ERROR_INVALID_THREAD_ID &&
               registry.windows().size() == 2,
           "registry mutation from another thread should be rejected");

    DestroyWindow(second);
    snapshots.resize(1);
    const ctm::ChromeWindowRegistryReport destroyed =
        registry.Synchronize(snapshots, first);
    Expect(destroyed.removed_count == 1 && registry.windows().size() == 1,
           "destroying a temporary top-level window should clean its record");
    DestroyWindow(first);
}

}  // namespace

int main() {
    TestCreateCloseReopenAndActiveFallback();
    TestTitleStormAndNoChangeAreIdempotent();
    TestIdentityReplacementAndDestroyHintResetSameIdentity();
    TestManagedWindowLimitRetainsTheExistingGroup();
    TestTemporaryTopLevelTitleDestroyAndMainThreadSerialization();

    if (failures != 0) {
        std::cerr << failures << " Chrome-window registry test(s) failed.\n";
        return 1;
    }
    std::cout << "All Chrome-window registry tests passed.\n";
    return 0;
}
