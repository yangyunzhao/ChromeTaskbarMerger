#include "window_coordinator.h"

#include "window_identity_query.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace ctm {
namespace {

[[nodiscard]] WindowActivationResult IdentityFailure(
    const WindowIdentity& expected,
    const bool require_visible) {
    const WindowIdentityQueryResult current =
        QueryWindowIdentity(expected.hwnd);
    if (!current.window_exists) {
        return {
            .win32_error = ERROR_INVALID_WINDOW_HANDLE,
            .message = L"The target window no longer exists.",
        };
    }
    if (!current.succeeded) {
        return {
            .win32_error = current.error_code,
            .message = L"The target window identity could not be queried.",
        };
    }
    if (!WindowIdentitiesMatch(expected, current.identity)) {
        return {
            .win32_error = ERROR_INVALID_WINDOW_HANDLE,
            .message = L"The HWND now belongs to a different window identity.",
        };
    }
    if (require_visible && IsWindowVisible(expected.hwnd) == FALSE) {
        return {
            .win32_error = ERROR_INVALID_STATE,
            .message = L"The target window is not visible.",
        };
    }
    if (IsWindowEnabled(expected.hwnd) == FALSE) {
        return {
            .win32_error = ERROR_ACCESS_DENIED,
            .message = L"The target window is disabled.",
        };
    }
    return {
        .succeeded = true,
        .message = L"The target window identity is current and activatable.",
    };
}

[[nodiscard]] int RectangleWidth(const RECT& rectangle) noexcept {
    return rectangle.right - rectangle.left;
}

[[nodiscard]] int RectangleHeight(const RECT& rectangle) noexcept {
    return rectangle.bottom - rectangle.top;
}

[[nodiscard]] RECT ConstrainToBounds(
    const RECT& rectangle,
    const RECT& bounds) noexcept {
    const int bounds_width = RectangleWidth(bounds);
    const int bounds_height = RectangleHeight(bounds);
    const int width = std::min(RectangleWidth(rectangle), bounds_width);
    const int height = std::min(RectangleHeight(rectangle), bounds_height);
    const int minimum_left = static_cast<int>(bounds.left);
    const int minimum_top = static_cast<int>(bounds.top);
    const int maximum_left = static_cast<int>(bounds.right) - width;
    const int maximum_top = static_cast<int>(bounds.bottom) - height;
    const int left = std::clamp(
        static_cast<int>(rectangle.left), minimum_left, maximum_left);
    const int top = std::clamp(
        static_cast<int>(rectangle.top), minimum_top, maximum_top);
    return {
        .left = left,
        .top = top,
        .right = left + width,
        .bottom = top + height,
    };
}

}  // namespace

WindowGroupGeometry CalculateWindowGroupGeometry(
    const RECT& group_bounds,
    const int tab_strip_height) noexcept {
    WindowGroupGeometry geometry;
    geometry.group_bounds = group_bounds;
    if (RectangleWidth(group_bounds) < 240 || tab_strip_height <= 0 ||
        RectangleHeight(group_bounds) < tab_strip_height + 160) {
        return geometry;
    }
    geometry.tab_strip_bounds = group_bounds;
    geometry.tab_strip_bounds.bottom =
        geometry.tab_strip_bounds.top + tab_strip_height;
    geometry.content_bounds = group_bounds;
    geometry.content_bounds.top = geometry.tab_strip_bounds.bottom;
    geometry.valid = true;
    return geometry;
}

WindowGroupGeometry CalculateWindowGroupGeometryFromContentBounds(
    const RECT& content_bounds,
    const RECT& work_area,
    const int tab_strip_height) noexcept {
    if (RectangleWidth(content_bounds) < 240 ||
        RectangleHeight(content_bounds) < 160 ||
        RectangleWidth(work_area) < 240 ||
        RectangleHeight(work_area) < tab_strip_height + 160 ||
        tab_strip_height <= 0) {
        return {};
    }
    const RECT desired_group = {
        .left = content_bounds.left,
        .top = content_bounds.top - tab_strip_height,
        .right = content_bounds.right,
        .bottom = content_bounds.bottom,
    };
    return CalculateWindowGroupGeometry(
        ConstrainToBounds(desired_group, work_area), tab_strip_height);
}

int ScalePixelsForDpi(const int pixels, const UINT dpi) noexcept {
    if (pixels <= 0 || dpi == 0) {
        return 0;
    }
    return MulDiv(pixels, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

bool RectanglesEqual(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

bool WindowGroupArrangementRequired(
    const WindowGroupGeometry& current,
    const WindowGroupGeometry& proposed,
    const RECT& driver_bounds) noexcept {
    if (!proposed.valid) {
        return false;
    }
    return !current.valid ||
           !RectanglesEqual(proposed.group_bounds, current.group_bounds) ||
           !RectanglesEqual(proposed.content_bounds, driver_bounds);
}

bool RectangleFitsWithin(const RECT& rectangle,
                         const RECT& bounds) noexcept {
    return RectangleWidth(rectangle) > 0 && RectangleHeight(rectangle) > 0 &&
           rectangle.left >= bounds.left && rectangle.top >= bounds.top &&
           rectangle.right <= bounds.right &&
           rectangle.bottom <= bounds.bottom;
}

bool IsFullscreenRectangle(const RECT& rectangle,
                           const RECT& monitor_bounds,
                           const int tolerance) noexcept {
    const int safe_tolerance = std::max(tolerance, 0);
    return std::abs(rectangle.left - monitor_bounds.left) <= safe_tolerance &&
           std::abs(rectangle.top - monitor_bounds.top) <= safe_tolerance &&
           std::abs(rectangle.right - monitor_bounds.right) <= safe_tolerance &&
           std::abs(rectangle.bottom - monitor_bounds.bottom) <= safe_tolerance;
}

WindowActivationResult Win32WindowActivationGateway::Verify(
    const WindowIdentity& identity) {
    return IdentityFailure(identity, true);
}

WindowActivationResult Win32WindowActivationGateway::Activate(
    const WindowIdentity& identity) {
    WindowActivationResult result = Verify(identity);
    if (!result.succeeded) {
        return result;
    }

    if (IsIconic(identity.hwnd) != FALSE &&
        ShowWindowAsync(identity.hwnd, SW_RESTORE) == FALSE) {
        result.succeeded = false;
        result.win32_error = GetLastError();
        result.message = L"Restoring the minimized target window failed.";
        return result;
    }
    if (SetWindowPos(
            identity.hwnd,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE) {
        result.succeeded = false;
        result.win32_error = GetLastError();
        result.message = L"Moving the target to the top of the group failed.";
        return result;
    }

    if (SetForegroundWindow(identity.hwnd) == FALSE &&
        GetForegroundWindow() != identity.hwnd) {
        result.succeeded = false;
        result.win32_error = ERROR_ACCESS_DENIED;
        result.message = L"Windows refused to activate the target window.";
        return result;
    }

    result.succeeded = true;
    result.win32_error = ERROR_SUCCESS;
    result.message = L"The target window was activated.";
    return result;
}

WindowCoordinationResult WindowGroupPlacementController::Capture(
    const std::span<const WindowIdentity> identities) {
    WindowCoordinationResult result;
    if (!records_.empty() || identities.empty()) {
        result.win32_error = ERROR_INVALID_STATE;
        result.message = records_.empty()
                             ? L"At least one window is required."
                             : L"Window placements were already captured.";
        return result;
    }

    std::vector<PlacementRecord> captured;
    captured.reserve(identities.size());
    for (const WindowIdentity& identity : identities) {
        result.identity = identity;
        const WindowActivationResult validation =
            IdentityFailure(identity, true);
        if (!validation.succeeded) {
            result.win32_error = validation.win32_error;
            result.message = validation.message;
            return result;
        }
        const bool minimized = IsIconic(identity.hwnd) != FALSE;
        const bool maximized = IsZoomed(identity.hwnd) != FALSE;
        if (minimized || maximized) {
            result.win32_error = ERROR_NOT_SUPPORTED;
            result.message = minimized
                                 ? L"The target window is minimized; restore it to normal first."
                                 : L"The target window is maximized; restore it to normal first.";
            return result;
        }

        PlacementRecord record;
        record.identity = identity;
        record.placement.length = sizeof(record.placement);
        if (GetWindowPlacement(identity.hwnd, &record.placement) == FALSE ||
            GetWindowRect(identity.hwnd, &record.rectangle) == FALSE) {
            result.win32_error = GetLastError();
            result.message = L"Capturing the original window layout failed.";
            return result;
        }
        record.participating = true;
        captured.push_back(record);
    }

    records_ = std::move(captured);
    result.succeeded = true;
    result.identity = {};
    result.message = L"The original window placements were captured.";
    return result;
}

WindowCoordinationResult
WindowGroupPlacementController::SynchronizeParticipants(
    const std::span<const WindowIdentity> identities) {
    WindowCoordinationResult result;
    std::vector<WindowIdentity> unique;
    unique.reserve(identities.size());
    for (const WindowIdentity& identity : identities) {
        const bool duplicate = std::any_of(
            unique.begin(),
            unique.end(),
            [&identity](const WindowIdentity& existing) {
                return existing.hwnd == identity.hwnd;
            });
        if (!WindowIdentityIsComplete(identity) || duplicate) {
            result.identity = identity;
            result.win32_error = ERROR_INVALID_DATA;
            result.message =
                L"A participating window identity is incomplete or duplicated.";
            return result;
        }
        unique.push_back(identity);
    }

    std::vector<PlacementRecord> captured;
    captured.reserve(unique.size());
    for (const WindowIdentity& identity : unique) {
        const auto existing = std::find_if(
            records_.begin(),
            records_.end(),
            [&identity](const PlacementRecord& record) {
                return !record.invalidated &&
                       WindowIdentitiesMatch(record.identity, identity);
            });
        if (existing != records_.end()) {
            continue;
        }

        result.identity = identity;
        const WindowActivationResult validation =
            IdentityFailure(identity, true);
        if (!validation.succeeded) {
            result.win32_error = validation.win32_error;
            result.message = validation.message;
            return result;
        }
        const bool minimized = IsIconic(identity.hwnd) != FALSE;
        const bool maximized = IsZoomed(identity.hwnd) != FALSE;
        if (minimized || maximized) {
            result.win32_error = ERROR_NOT_SUPPORTED;
            result.message = minimized
                                 ? L"The new group member is minimized; restore it to normal first."
                                 : L"The new group member is maximized; restore it to normal first.";
            return result;
        }

        PlacementRecord record;
        record.identity = identity;
        record.placement.length = sizeof(record.placement);
        if (GetWindowPlacement(identity.hwnd, &record.placement) == FALSE ||
            GetWindowRect(identity.hwnd, &record.rectangle) == FALSE) {
            result.win32_error = GetLastError();
            result.message =
                L"Capturing a new member's original window layout failed.";
            return result;
        }
        record.participating = true;
        captured.push_back(record);
    }

    for (PlacementRecord& record : records_) {
        record.participating = false;
    }
    for (const WindowIdentity& identity : unique) {
        const auto existing = std::find_if(
            records_.begin(),
            records_.end(),
            [&identity](const PlacementRecord& record) {
                return !record.invalidated &&
                       WindowIdentitiesMatch(record.identity, identity);
            });
        if (existing != records_.end()) {
            existing->participating = true;
        }
    }
    records_.insert(
        records_.end(),
        std::make_move_iterator(captured.begin()),
        std::make_move_iterator(captured.end()));
    result.succeeded = true;
    result.identity = {};
    result.message =
        L"The captured window set matches the current group membership.";
    return result;
}

std::size_t WindowGroupPlacementController::InvalidateHandles(
    const std::span<const HWND> destroyed_handles) noexcept {
    std::size_t invalidated_count = 0;
    for (PlacementRecord& record : records_) {
        if (!record.invalidated &&
            std::find(
                destroyed_handles.begin(),
                destroyed_handles.end(),
                record.identity.hwnd) != destroyed_handles.end()) {
            record.participating = false;
            record.invalidated = true;
            record.restore_completed = true;
            ++invalidated_count;
        }
    }
    return invalidated_count;
}

WindowCoordinationResult WindowGroupPlacementController::Arrange(
    const WindowGroupGeometry& geometry,
    const WindowIdentity& active_identity) {
    WindowCoordinationResult result;
    const bool has_participant = std::any_of(
        records_.begin(),
        records_.end(),
        [](const PlacementRecord& record) {
            return record.participating && !record.invalidated;
        });
    if (!geometry.valid || !has_participant) {
        result.win32_error = ERROR_INVALID_STATE;
        result.message = L"The group geometry or captured layout is unavailable.";
        return result;
    }

    const auto active = std::find_if(
        records_.begin(),
        records_.end(),
        [&active_identity](const PlacementRecord& record) {
            return record.participating && !record.invalidated &&
                   WindowIdentitiesMatch(
                record.identity, active_identity);
        });
    if (active == records_.end()) {
        result.win32_error = ERROR_NOT_FOUND;
        result.message = L"The active window is not part of the captured group.";
        return result;
    }

    const int width = RectangleWidth(geometry.content_bounds);
    const int height = RectangleHeight(geometry.content_bounds);
    std::vector<PlacementRecord*> participants;
    participants.reserve(records_.size());
    for (PlacementRecord& record : records_) {
        if (!record.participating || record.invalidated) {
            continue;
        }
        result.identity = record.identity;
        const WindowActivationResult validation =
            IdentityFailure(record.identity, true);
        if (!validation.succeeded) {
            result.win32_error = validation.win32_error;
            result.message = validation.message;
            return result;
        }
        participants.push_back(&record);
    }

    HDWP deferred = BeginDeferWindowPos(
        static_cast<int>(participants.size()));
    if (deferred == nullptr) {
        result.win32_error = GetLastError();
        result.message = L"Starting the atomic group arrangement failed.";
        return result;
    }
    for (PlacementRecord* const record : participants) {
        result.identity = record->identity;
        const bool is_active = WindowIdentitiesMatch(
            record->identity, active_identity);
        deferred = DeferWindowPos(
            deferred,
            record->identity.hwnd,
            is_active ? HWND_TOP : active_identity.hwnd,
            geometry.content_bounds.left,
            geometry.content_bounds.top,
            width,
            height,
            SWP_NOACTIVATE);
        if (deferred == nullptr) {
            result.win32_error = GetLastError();
            result.message =
                L"Queuing the atomic group rectangle and Z-order failed.";
            return result;
        }
    }
    if (EndDeferWindowPos(deferred) == FALSE) {
        result.win32_error = GetLastError();
        result.message = L"Committing the atomic group arrangement failed.";
        return result;
    }
    for (PlacementRecord* const record : participants) {
        record->position_modified = true;
    }

    result.succeeded = true;
    result.identity = {};
    result.message = L"The windows now share one group rectangle.";
    return result;
}

WindowCoordinationResult WindowGroupPlacementController::ArrangeAsNormal(
    const WindowGroupGeometry& geometry,
    const WindowIdentity& active_identity) {
    WindowCoordinationResult result;
    if (!geometry.valid) {
        result.win32_error = ERROR_INVALID_STATE;
        result.message = L"The managed-normal group geometry is unavailable.";
        return result;
    }

    std::vector<PlacementRecord*> participants;
    participants.reserve(records_.size());
    bool active_found = false;
    for (PlacementRecord& record : records_) {
        if (!record.participating || record.invalidated) {
            continue;
        }
        result.identity = record.identity;
        const WindowActivationResult validation =
            IdentityFailure(record.identity, true);
        if (!validation.succeeded) {
            result.win32_error = validation.win32_error;
            result.message = validation.message;
            return result;
        }
        active_found = active_found || WindowIdentitiesMatch(
            record.identity, active_identity);
        participants.push_back(&record);
    }
    if (participants.empty() || !active_found) {
        result.win32_error = ERROR_NOT_FOUND;
        result.message =
            L"The active window is not part of the managed-normal group.";
        return result;
    }

    // The restore obligation must exist before the first native state change.
    // This also makes a partial normalization safe for the session guard.
    for (PlacementRecord* const record : participants) {
        record->position_modified = true;
    }
    for (PlacementRecord* const record : participants) {
        result.identity = record->identity;
        WINDOWPLACEMENT current{};
        current.length = sizeof(current);
        if (GetWindowPlacement(record->identity.hwnd, &current) == FALSE) {
            result.win32_error = GetLastError();
            result.message =
                L"Reading native state before managed-normal arrangement failed.";
            return result;
        }
        // Do not inherit asynchronous or restore-to-maximized placement
        // flags from a minimized/maximized member. The normal-state check
        // below must observe this transition before the atomic arrangement.
        current.flags = 0;
        current.showCmd = SW_SHOWNOACTIVATE;
        if (SetWindowPlacement(record->identity.hwnd, &current) == FALSE) {
            result.win32_error = GetLastError();
            result.message =
                L"Restoring native state for managed-normal arrangement failed.";
            return result;
        }
        if (IsIconic(record->identity.hwnd) != FALSE ||
            IsZoomed(record->identity.hwnd) != FALSE) {
            result.win32_error = ERROR_INVALID_STATE;
            result.message =
                L"The window did not enter native normal state for safe arrangement.";
            return result;
        }
    }

    return Arrange(geometry, active_identity);
}

WindowGroupRestoreReport WindowGroupPlacementController::RestoreAll() {
    WindowGroupRestoreReport report;
    for (auto record = records_.rbegin(); record != records_.rend(); ++record) {
        if (!record->position_modified || record->restore_completed) {
            continue;
        }

        WindowCoordinationResult operation;
        operation.identity = record->identity;
        const WindowIdentityQueryResult current =
            QueryWindowIdentity(record->identity.hwnd);
        if (!current.window_exists ||
            (current.succeeded &&
             !WindowIdentitiesMatch(record->identity, current.identity))) {
            record->restore_completed = true;
            operation.succeeded = true;
            operation.skipped = true;
            operation.message =
                L"The original identity no longer exists; layout restore was safely skipped.";
            ++report.safely_skipped_count;
            report.operations.push_back(std::move(operation));
            continue;
        }
        if (!current.succeeded) {
            operation.win32_error = current.error_code;
            operation.message =
                L"The current identity could not be verified for layout restore.";
            report.succeeded = false;
            report.operations.push_back(std::move(operation));
            continue;
        }

        WINDOWPLACEMENT placement = record->placement;
        placement.length = sizeof(placement);
        if (SetWindowPlacement(record->identity.hwnd, &placement) == FALSE) {
            operation.win32_error = GetLastError();
            operation.message = L"SetWindowPlacement failed during restore.";
            report.succeeded = false;
            report.operations.push_back(std::move(operation));
            continue;
        }

        const int width = RectangleWidth(record->rectangle);
        const int height = RectangleHeight(record->rectangle);
        if (SetWindowPos(
                record->identity.hwnd,
                nullptr,
                record->rectangle.left,
                record->rectangle.top,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE) == FALSE) {
            operation.win32_error = GetLastError();
            operation.message = L"Restoring the exact window rectangle failed.";
            report.succeeded = false;
            report.operations.push_back(std::move(operation));
            continue;
        }

        record->restore_completed = true;
        operation.succeeded = true;
        operation.message = L"The original window layout was restored.";
        ++report.restored_count;
        report.operations.push_back(std::move(operation));
    }
    return report;
}

bool WindowGroupPlacementController::needs_restore() const noexcept {
    return std::any_of(
        records_.begin(),
        records_.end(),
        [](const PlacementRecord& record) {
            return record.position_modified && !record.restore_completed;
        });
}

}  // namespace ctm
