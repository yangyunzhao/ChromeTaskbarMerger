#include "taskbar_controller.h"

#include "window_identity_query.h"

#include <utility>

namespace ctm {
namespace {

struct ExtendedStyleResult {
    LONG_PTR value = 0;
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] ExtendedStyleResult QueryExtendedStyle(const HWND hwnd) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR value = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const DWORD error_code = GetLastError();
    return {
        .value = value,
        .succeeded = value != 0 || error_code == ERROR_SUCCESS,
        .error_code = error_code,
    };
}

[[nodiscard]] bool RefreshTaskbarFrame(const HWND hwnd,
                                       DWORD* const error_code) {
    if (SetWindowPos(
            hwnd,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_FRAMECHANGED) == FALSE) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        return false;
    }

    if (error_code != nullptr) {
        *error_code = ERROR_SUCCESS;
    }
    return true;
}

[[nodiscard]] bool SetExtendedStyle(const HWND hwnd,
                                    const LONG_PTR value,
                                    DWORD* const error_code) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, value);
    const DWORD operation_error = GetLastError();
    if (previous == 0 && operation_error != ERROR_SUCCESS) {
        if (error_code != nullptr) {
            *error_code = operation_error;
        }
        return false;
    }

    if (error_code != nullptr) {
        *error_code = ERROR_SUCCESS;
    }
    return true;
}

[[nodiscard]] TaskbarOperationResult ValidateRestoreTarget(
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    if (state == nullptr) {
        result.win32_error = ERROR_INVALID_PARAMETER;
        result.message = L"The mutation state pointer is null.";
        return result;
    }

    if (!state->NeedsRestore()) {
        result.succeeded = true;
        result.skipped = true;
        result.message = L"No applied taskbar modification needs restoration.";
        return result;
    }

    const WindowIdentityQueryResult current =
        QueryWindowIdentity(state->identity.hwnd);
    if (!current.window_exists) {
        state->restore_completed = true;
        result.succeeded = true;
        result.skipped = true;
        result.message =
            L"The original window no longer exists; no restoration is needed.";
        return result;
    }

    if (!current.succeeded) {
        result.win32_error = current.error_code;
        result.message = L"The target window identity could not be queried.";
        return result;
    }

    if (!WindowIdentityValuesMatch(
            state->identity,
            current.identity.process_id,
            current.identity.thread_id,
            current.identity.process_creation_time,
            current.identity.class_name)) {
        state->restore_completed = true;
        result.succeeded = true;
        result.skipped = true;
        result.message =
            L"The HWND now belongs to a different window; restoration was "
            L"safely skipped.";
        return result;
    }

    result.succeeded = true;
    return result;
}

}  // namespace

LONG_PTR CalculateTaskbarHiddenExtendedStyle(
    const LONG_PTR original_extended_style) noexcept {
    return (original_extended_style & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) |
           static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
}

std::wstring_view TaskbarMethodText(const TaskbarMethod method) {
    switch (method) {
        case TaskbarMethod::TaskbarList:
            return L"ITaskbarList::DeleteTab/AddTab";
        case TaskbarMethod::WindowStyle:
            return L"WS_EX_APPWINDOW/WS_EX_TOOLWINDOW";
    }
    return L"unknown taskbar method";
}

TaskbarController::~TaskbarController() {
    Shutdown();
}

TaskbarOperationResult TaskbarController::InitializeTaskbarList() {
    TaskbarOperationResult result;
    if (taskbar_list_ != nullptr) {
        result.succeeded = true;
        result.message = L"ITaskbarList is already initialized.";
        return result;
    }

    const HRESULT initialize_result =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    result.hresult = initialize_result;
    if (FAILED(initialize_result)) {
        result.message = L"CoInitializeEx failed.";
        return result;
    }
    com_initialized_ = true;

    const HRESULT create_result = CoCreateInstance(
        CLSID_TaskbarList,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(taskbar_list_.ReleaseAndGetAddressOf()));
    result.hresult = create_result;
    if (FAILED(create_result)) {
        result.message = L"CoCreateInstance(CLSID_TaskbarList) failed.";
        Shutdown();
        return result;
    }

    const HRESULT taskbar_initialize_result = taskbar_list_->HrInit();
    result.hresult = taskbar_initialize_result;
    if (FAILED(taskbar_initialize_result)) {
        result.message = L"ITaskbarList::HrInit failed.";
        Shutdown();
        return result;
    }

    result.succeeded = true;
    result.message = L"ITaskbarList initialized successfully.";
    return result;
}

TaskbarOperationResult TaskbarController::RemoveWindow(
    const ChromeWindowSnapshot& snapshot,
    const TaskbarMethod method,
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    if (state == nullptr) {
        result.win32_error = ERROR_INVALID_PARAMETER;
        result.message = L"The mutation state pointer is null.";
        return result;
    }
    if (state->NeedsRestore()) {
        result.win32_error = ERROR_BUSY;
        result.message =
            L"A previous taskbar modification still needs restoration.";
        return result;
    }

    const WindowIdentityQueryResult current =
        QueryWindowIdentity(snapshot.hwnd);
    if (!current.succeeded) {
        result.win32_error = current.error_code;
        result.message = L"The selected window is no longer valid.";
        return result;
    }
    if (snapshot.process_id != current.identity.process_id ||
        snapshot.thread_id != current.identity.thread_id ||
        snapshot.process_creation_time !=
            current.identity.process_creation_time ||
        snapshot.class_name != current.identity.class_name) {
        result.win32_error = ERROR_INVALID_WINDOW_HANDLE;
        result.message =
            L"The selected HWND identity changed after enumeration.";
        return result;
    }

    *state = {};
    state->method = method;
    state->identity = current.identity;

    if (method == TaskbarMethod::TaskbarList) {
        const TaskbarOperationResult initialization = InitializeTaskbarList();
        if (!initialization.succeeded) {
            return initialization;
        }
        return RemoveWithTaskbarList(state);
    }
    return RemoveWithWindowStyle(state);
}

TaskbarOperationResult TaskbarController::RestoreWindow(
    TaskbarMutationState* const state) {
    TaskbarOperationResult validation = ValidateRestoreTarget(state);
    if (!validation.succeeded || validation.skipped) {
        return validation;
    }

    if (state->method == TaskbarMethod::TaskbarList) {
        const TaskbarOperationResult initialization = InitializeTaskbarList();
        if (!initialization.succeeded) {
            return initialization;
        }
        return RestoreWithTaskbarList(state);
    }
    return RestoreWithWindowStyle(state);
}

TaskbarOperationResult TaskbarController::ForceRestoreWindow(
    const ChromeWindowSnapshot& snapshot) {
    TaskbarOperationResult result;
    const WindowIdentityQueryResult current =
        QueryWindowIdentity(snapshot.hwnd);
    if (!current.succeeded) {
        result.win32_error = current.error_code;
        result.message = L"The explicit restore target is no longer valid.";
        return result;
    }
    if (snapshot.process_id != current.identity.process_id ||
        snapshot.thread_id != current.identity.thread_id ||
        snapshot.process_creation_time !=
            current.identity.process_creation_time ||
        snapshot.class_name != current.identity.class_name) {
        result.win32_error = ERROR_INVALID_WINDOW_HANDLE;
        result.message =
            L"The explicit restore target identity changed after enumeration.";
        return result;
    }

    const TaskbarOperationResult initialization = InitializeTaskbarList();
    if (!initialization.succeeded) {
        return initialization;
    }

    const HRESULT add_result = taskbar_list_->AddTab(snapshot.hwnd);
    result.hresult = add_result;
    if (FAILED(add_result)) {
        result.message = L"Explicit ITaskbarList::AddTab failed.";
        return result;
    }
    result.succeeded = true;
    result.state_changed = true;
    result.message = L"Explicit AddTab returned success.";
    return result;
}

TaskbarOperationResult TaskbarController::RemoveWithTaskbarList(
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    const ExtendedStyleResult before =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_before = before.value;

    const HRESULT delete_result =
        taskbar_list_->DeleteTab(state->identity.hwnd);
    result.hresult = delete_result;
    if (FAILED(delete_result)) {
        result.message = L"ITaskbarList::DeleteTab failed.";
        return result;
    }

    state->modification_applied = true;
    result.succeeded = true;
    result.state_changed = true;
    result.extended_style_after = before.value;
    result.message =
        L"DeleteTab returned success; visual confirmation is still required.";
    return result;
}

TaskbarOperationResult TaskbarController::RemoveWithWindowStyle(
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    const ExtendedStyleResult before =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_before = before.value;
    if (!before.succeeded) {
        result.win32_error = before.error_code;
        result.message = L"Reading the original extended style failed.";
        return result;
    }

    state->original_extended_style = before.value;
    state->applied_extended_style =
        CalculateTaskbarHiddenExtendedStyle(before.value);

    DWORD operation_error = ERROR_SUCCESS;
    if (!SetExtendedStyle(
            state->identity.hwnd,
            state->applied_extended_style,
            &operation_error)) {
        result.win32_error = operation_error;
        result.message = L"SetWindowLongPtrW failed while removing the entry.";
        return result;
    }

    state->modification_applied = true;
    result.state_changed =
        state->applied_extended_style != state->original_extended_style;
    if (!RefreshTaskbarFrame(state->identity.hwnd, &operation_error)) {
        result.win32_error = operation_error;
        result.message =
            L"The style changed, but SetWindowPos(SWP_FRAMECHANGED) failed; "
            L"restoration is required.";
        return result;
    }

    const ExtendedStyleResult after =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_after = after.value;
    if (!after.succeeded) {
        result.win32_error = after.error_code;
        result.message =
            L"The style changed, but verification failed; restoration is "
            L"required.";
        return result;
    }
    if (after.value != state->applied_extended_style) {
        result.win32_error = ERROR_INVALID_DATA;
        result.message =
            L"Chrome changed the extended style unexpectedly; restoration is "
            L"required.";
        return result;
    }

    result.succeeded = true;
    result.message =
        L"The extended style changed successfully; visual confirmation is "
        L"still required.";
    return result;
}

TaskbarOperationResult TaskbarController::RestoreWithTaskbarList(
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    const ExtendedStyleResult before =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_before = before.value;

    const HRESULT add_result = taskbar_list_->AddTab(state->identity.hwnd);
    result.hresult = add_result;
    if (FAILED(add_result)) {
        result.message = L"ITaskbarList::AddTab failed.";
        return result;
    }

    state->restore_completed = true;
    result.succeeded = true;
    result.state_changed = true;
    result.extended_style_after = before.value;
    result.message =
        L"AddTab returned success; visual confirmation is still required.";
    return result;
}

TaskbarOperationResult TaskbarController::RestoreWithWindowStyle(
    TaskbarMutationState* const state) {
    TaskbarOperationResult result;
    const ExtendedStyleResult before =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_before = before.value;

    DWORD operation_error = ERROR_SUCCESS;
    if (!SetExtendedStyle(
            state->identity.hwnd,
            state->original_extended_style,
            &operation_error)) {
        result.win32_error = operation_error;
        result.message = L"SetWindowLongPtrW failed during restoration.";
        return result;
    }

    if (!RefreshTaskbarFrame(state->identity.hwnd, &operation_error)) {
        result.win32_error = operation_error;
        result.message =
            L"The original style was written, but frame refresh failed.";
        return result;
    }

    const ExtendedStyleResult after =
        QueryExtendedStyle(state->identity.hwnd);
    result.extended_style_after = after.value;
    if (!after.succeeded) {
        result.win32_error = after.error_code;
        result.message = L"Reading the restored extended style failed.";
        return result;
    }
    if (after.value != state->original_extended_style) {
        result.win32_error = ERROR_INVALID_DATA;
        result.message =
            L"The restored extended style does not match the original value.";
        return result;
    }

    state->restore_completed = true;
    result.succeeded = true;
    result.state_changed = before.value != after.value;
    result.message = L"The original extended style was restored exactly.";
    return result;
}

void TaskbarController::Shutdown() {
    taskbar_list_.Reset();
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

}  // namespace ctm
