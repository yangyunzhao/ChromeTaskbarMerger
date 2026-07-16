#include "window_coordinator.h"

#include "window_identity_query.h"

#include <algorithm>

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
        if (IsIconic(identity.hwnd) != FALSE ||
            IsZoomed(identity.hwnd) != FALSE) {
            result.win32_error = ERROR_NOT_SUPPORTED;
            result.message =
                L"Phase 1 accepts only normal, non-minimized windows.";
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
        captured.push_back(record);
    }

    records_ = std::move(captured);
    result.succeeded = true;
    result.identity = {};
    result.message = L"The original window placements were captured.";
    return result;
}

WindowCoordinationResult WindowGroupPlacementController::Arrange(
    const WindowGroupGeometry& geometry,
    const WindowIdentity& active_identity) {
    WindowCoordinationResult result;
    if (!geometry.valid || records_.empty()) {
        result.win32_error = ERROR_INVALID_STATE;
        result.message = L"The group geometry or captured layout is unavailable.";
        return result;
    }

    const auto active = std::find_if(
        records_.begin(),
        records_.end(),
        [&active_identity](const PlacementRecord& record) {
            return WindowIdentitiesMatch(
                record.identity, active_identity);
        });
    if (active == records_.end()) {
        result.win32_error = ERROR_NOT_FOUND;
        result.message = L"The active window is not part of the captured group.";
        return result;
    }

    const int width = RectangleWidth(geometry.content_bounds);
    const int height = RectangleHeight(geometry.content_bounds);
    for (PlacementRecord& record : records_) {
        result.identity = record.identity;
        const WindowActivationResult validation =
            IdentityFailure(record.identity, true);
        if (!validation.succeeded) {
            result.win32_error = validation.win32_error;
            result.message = validation.message;
            return result;
        }
        if (SetWindowPos(
                record.identity.hwnd,
                nullptr,
                geometry.content_bounds.left,
                geometry.content_bounds.top,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE) == FALSE) {
            result.win32_error = GetLastError();
            result.message = L"Applying the shared group rectangle failed.";
            return result;
        }
        record.position_modified = true;
    }

    for (const PlacementRecord& record : records_) {
        if (WindowIdentitiesMatch(record.identity, active_identity)) {
            continue;
        }
        if (SetWindowPos(
                record.identity.hwnd,
                active_identity.hwnd,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE) {
            result.identity = record.identity;
            result.win32_error = GetLastError();
            result.message = L"Applying the group Z-order failed.";
            return result;
        }
    }
    if (SetWindowPos(
            active_identity.hwnd,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE) {
        result.identity = active_identity;
        result.win32_error = GetLastError();
        result.message = L"Raising the initial active window failed.";
        return result;
    }

    result.succeeded = true;
    result.identity = {};
    result.message = L"The windows now share one group rectangle.";
    return result;
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
