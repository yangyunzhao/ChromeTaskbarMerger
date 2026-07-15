#pragma once

#include "chrome_window.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

#include <cstdint>
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
    std::uint64_t process_creation_time = 0;
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
    std::uint64_t process_creation_time,
    std::wstring_view class_name) noexcept;
[[nodiscard]] std::wstring_view TaskbarMethodText(TaskbarMethod method);

class ITaskbarMutationController {
public:
    virtual ~ITaskbarMutationController() = default;

    [[nodiscard]] virtual TaskbarOperationResult RemoveWindow(
        const ChromeWindowSnapshot& snapshot,
        TaskbarMethod method,
        TaskbarMutationState* state) = 0;
    [[nodiscard]] virtual TaskbarOperationResult RestoreWindow(
        TaskbarMutationState* state) = 0;
};

class TaskbarController final : public ITaskbarMutationController {
public:
    TaskbarController() = default;
    ~TaskbarController() override;

    TaskbarController(const TaskbarController&) = delete;
    TaskbarController& operator=(const TaskbarController&) = delete;
    TaskbarController(TaskbarController&&) = delete;
    TaskbarController& operator=(TaskbarController&&) = delete;

    [[nodiscard]] TaskbarOperationResult InitializeTaskbarList();
    [[nodiscard]] TaskbarOperationResult RemoveWindow(
        const ChromeWindowSnapshot& snapshot,
        TaskbarMethod method,
        TaskbarMutationState* state) override;
    [[nodiscard]] TaskbarOperationResult RestoreWindow(
        TaskbarMutationState* state) override;
    [[nodiscard]] TaskbarOperationResult ForceRestoreWindow(
        const ChromeWindowSnapshot& snapshot);
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
