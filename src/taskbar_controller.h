#pragma once

#include "chrome_window.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace ctm {

enum class TaskbarMethod {
    TaskbarList,
    WindowStyle,
};

struct WindowIdentity {
    HWND hwnd = nullptr;
    DWORD process_id = 0;
    DWORD thread_id = 0;
    std::wstring class_name;
};

struct TaskbarMutationState {
    TaskbarMethod method = TaskbarMethod::TaskbarList;
    WindowIdentity identity;
    LONG_PTR original_extended_style = 0;
    LONG_PTR applied_extended_style = 0;
    bool modification_applied = false;
    bool restore_completed = false;

    [[nodiscard]] bool NeedsRestore() const noexcept {
        return modification_applied && !restore_completed;
    }
};

struct TaskbarOperationResult {
    bool succeeded = false;
    bool state_changed = false;
    bool skipped = false;
    HRESULT hresult = S_OK;
    DWORD win32_error = ERROR_SUCCESS;
    LONG_PTR extended_style_before = 0;
    LONG_PTR extended_style_after = 0;
    std::wstring message;
};

[[nodiscard]] LONG_PTR CalculateTaskbarHiddenExtendedStyle(
    LONG_PTR original_extended_style) noexcept;
[[nodiscard]] bool WindowIdentityValuesMatch(
    const WindowIdentity& expected,
    DWORD process_id,
    DWORD thread_id,
    std::wstring_view class_name) noexcept;
[[nodiscard]] std::wstring_view TaskbarMethodText(TaskbarMethod method);

class TaskbarController final {
public:
    TaskbarController() = default;
    ~TaskbarController();

    TaskbarController(const TaskbarController&) = delete;
    TaskbarController& operator=(const TaskbarController&) = delete;
    TaskbarController(TaskbarController&&) = delete;
    TaskbarController& operator=(TaskbarController&&) = delete;

    [[nodiscard]] TaskbarOperationResult InitializeTaskbarList();
    [[nodiscard]] TaskbarOperationResult RemoveWindow(
        const ChromeWindowSnapshot& snapshot,
        TaskbarMethod method,
        TaskbarMutationState* state);
    [[nodiscard]] TaskbarOperationResult RestoreWindow(
        TaskbarMutationState* state);
    void Shutdown();

private:
    [[nodiscard]] TaskbarOperationResult RemoveWithTaskbarList(
        TaskbarMutationState* state);
    [[nodiscard]] TaskbarOperationResult RemoveWithWindowStyle(
        TaskbarMutationState* state);
    [[nodiscard]] TaskbarOperationResult RestoreWithTaskbarList(
        TaskbarMutationState* state);
    [[nodiscard]] TaskbarOperationResult RestoreWithWindowStyle(
        TaskbarMutationState* state);

    bool com_initialized_ = false;
    Microsoft::WRL::ComPtr<ITaskbarList> taskbar_list_;
};

}  // namespace ctm
