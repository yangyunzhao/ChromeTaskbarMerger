#pragma once

#include "taskbar_controller.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ctm {

enum class FixedEntryOperationKind {
    Remove,
    Restore,
};

struct FixedEntryOperation {
    FixedEntryOperationKind kind = FixedEntryOperationKind::Remove;
    WindowIdentity identity;
    TaskbarOperationResult result;
};

struct FixedEntryReport {
    bool succeeded = true;
    std::size_t manageable_window_count = 0;
    std::optional<WindowIdentity> main_entry;
    bool main_entry_changed = false;
    std::size_t already_removed_count = 0;
    std::vector<FixedEntryOperation> operations;
    std::wstring message;
};

class FixedEntryManager final {
public:
    explicit FixedEntryManager(ITaskbarMutationController* controller)
        : controller_(controller) {}

    FixedEntryManager(const FixedEntryManager&) = delete;
    FixedEntryManager& operator=(const FixedEntryManager&) = delete;
    FixedEntryManager(FixedEntryManager&&) = delete;
    FixedEntryManager& operator=(FixedEntryManager&&) = delete;

    [[nodiscard]] FixedEntryReport Synchronize(
        std::span<const ChromeWindowSnapshot> windows,
        HWND foreground_window);
    [[nodiscard]] FixedEntryReport RestoreAll();

    [[nodiscard]] const std::optional<WindowIdentity>& main_entry()
        const noexcept {
        return main_entry_;
    }

    [[nodiscard]] std::size_t removed_window_count() const noexcept {
        return removed_windows_.size();
    }

private:
    ITaskbarMutationController* controller_ = nullptr;
    std::optional<WindowIdentity> main_entry_;
    std::vector<TaskbarMutationState> removed_windows_;
    std::size_t last_window_count_ = 0;
};

}  // namespace ctm
