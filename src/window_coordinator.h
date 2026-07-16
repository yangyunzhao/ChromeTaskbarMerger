#pragma once

#include "tab_activation.h"
#include "window_identity.h"

#include <Windows.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace ctm {

struct WindowGroupGeometry {
    bool valid = false;
    RECT group_bounds{};
    RECT tab_strip_bounds{};
    RECT content_bounds{};
};

[[nodiscard]] WindowGroupGeometry CalculateWindowGroupGeometry(
    const RECT& group_bounds,
    int tab_strip_height) noexcept;

struct WindowCoordinationResult {
    bool succeeded = false;
    bool skipped = false;
    DWORD win32_error = ERROR_SUCCESS;
    WindowIdentity identity;
    std::wstring message;
};

struct WindowGroupRestoreReport {
    bool succeeded = true;
    std::size_t restored_count = 0;
    std::size_t safely_skipped_count = 0;
    std::vector<WindowCoordinationResult> operations;
};

class Win32WindowActivationGateway final
    : public IWindowActivationGateway {
public:
    [[nodiscard]] WindowActivationResult Verify(
        const WindowIdentity& identity) override;
    [[nodiscard]] WindowActivationResult Activate(
        const WindowIdentity& identity) override;
};

class WindowGroupPlacementController final {
public:
    [[nodiscard]] WindowCoordinationResult Capture(
        std::span<const WindowIdentity> identities);
    [[nodiscard]] WindowCoordinationResult SynchronizeParticipants(
        std::span<const WindowIdentity> identities);
    [[nodiscard]] std::size_t InvalidateHandles(
        std::span<const HWND> destroyed_handles) noexcept;
    [[nodiscard]] WindowCoordinationResult Arrange(
        const WindowGroupGeometry& geometry,
        const WindowIdentity& active_identity);
    [[nodiscard]] WindowGroupRestoreReport RestoreAll();

    [[nodiscard]] bool needs_restore() const noexcept;
    [[nodiscard]] std::size_t captured_window_count() const noexcept {
        return records_.size();
    }

private:
    struct PlacementRecord {
        WindowIdentity identity;
        WINDOWPLACEMENT placement{};
        RECT rectangle{};
        bool position_modified = false;
        bool restore_completed = false;
        bool participating = false;
        bool invalidated = false;
    };

    std::vector<PlacementRecord> records_;
};

}  // namespace ctm
