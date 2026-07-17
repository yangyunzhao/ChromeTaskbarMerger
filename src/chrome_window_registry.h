#pragma once

#include "chrome_window.h"
#include "window_identity.h"

#include <Windows.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace ctm {

struct ChromeWindowRegistryReport {
    bool succeeded = true;
    DWORD win32_error = ERROR_SUCCESS;
    std::size_t added_count = 0;
    std::size_t removed_count = 0;
    std::size_t replaced_identity_count = 0;
    std::size_t updated_title_count = 0;
    std::size_t ignored_snapshot_count = 0;
    bool active_changed = false;
    std::vector<ChromeWindowSnapshot> added;
    std::vector<ChromeWindowSnapshot> removed;

    [[nodiscard]] bool HasMembershipChanges() const noexcept {
        return added_count != 0 || removed_count != 0 ||
               replaced_identity_count != 0;
    }

    [[nodiscard]] bool HasVisualChanges() const noexcept {
        return HasMembershipChanges() || updated_title_count != 0 ||
               active_changed;
    }
};

struct ManagedChromeWindowSelection {
    std::vector<ChromeWindowSnapshot> selected;
    std::vector<ChromeWindowSnapshot> overflow;
};

[[nodiscard]] ManagedChromeWindowSelection SelectManagedChromeWindows(
    std::span<const ChromeWindowSnapshot> candidates,
    std::span<const ChromeWindowSnapshot> currently_managed,
    HWND foreground_window,
    std::size_t maximum_count);

class ChromeWindowRegistry final {
public:
    ChromeWindowRegistry() noexcept;

    [[nodiscard]] ChromeWindowRegistryReport Synchronize(
        std::span<const ChromeWindowSnapshot> snapshots,
        HWND foreground_window = nullptr);
    [[nodiscard]] ChromeWindowRegistryReport InvalidateHandles(
        std::span<const HWND> destroyed_handles);

    [[nodiscard]] std::span<const ChromeWindowSnapshot> windows()
        const noexcept {
        return windows_;
    }
    [[nodiscard]] const std::optional<WindowIdentity>& active_identity()
        const noexcept {
        return active_identity_;
    }
    [[nodiscard]] DWORD owner_thread_id() const noexcept {
        return owner_thread_id_;
    }

private:
    [[nodiscard]] bool CheckThread(
        ChromeWindowRegistryReport* report) const noexcept;

    DWORD owner_thread_id_ = 0;
    std::vector<ChromeWindowSnapshot> windows_;
    std::optional<WindowIdentity> active_identity_;
};

[[nodiscard]] WindowIdentity MakeWindowIdentity(
    const ChromeWindowSnapshot& snapshot);

}  // namespace ctm
