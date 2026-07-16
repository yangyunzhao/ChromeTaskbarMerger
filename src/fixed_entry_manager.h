#pragma once

#include "recovery_journal.h"
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
    std::wstring persistence_error;
    std::wstring readiness_error;
    std::wstring message;
};

class IFixedEntryReadinessGate {
public:
    virtual ~IFixedEntryReadinessGate() = default;

    [[nodiscard]] virtual bool ConfirmReadyAfterRecoveryWrite(
        const WindowIdentity& identity,
        std::wstring* error_message) = 0;
    virtual void RecoveryIntentCleared(
        const WindowIdentity& identity) noexcept = 0;
};

class FixedEntryManager final {
public:
    explicit FixedEntryManager(
        ITaskbarMutationController* controller,
        IRecoveryStateStore* recovery_store = nullptr,
        IFixedEntryReadinessGate* readiness_gate = nullptr)
        : controller_(controller),
          recovery_store_(recovery_store),
          readiness_gate_(readiness_gate) {}

    FixedEntryManager(const FixedEntryManager&) = delete;
    FixedEntryManager& operator=(const FixedEntryManager&) = delete;
    FixedEntryManager(FixedEntryManager&&) = delete;
    FixedEntryManager& operator=(FixedEntryManager&&) = delete;

    [[nodiscard]] FixedEntryReport Synchronize(
        std::span<const ChromeWindowSnapshot> windows,
        HWND foreground_window);
    [[nodiscard]] FixedEntryReport RestoreAll();
    [[nodiscard]] bool AdoptRecoveryStates(
        std::vector<TaskbarMutationState> states,
        std::wstring* error_message);
    [[nodiscard]] bool ResetAfterTaskbarRecreation(
        std::wstring* error_message);

    [[nodiscard]] const std::optional<WindowIdentity>& main_entry()
        const noexcept {
        return main_entry_;
    }

    [[nodiscard]] std::size_t removed_window_count() const noexcept {
        return removed_windows_.size();
    }

    [[nodiscard]] std::span<const TaskbarMutationState> removed_windows()
        const noexcept {
        return removed_windows_;
    }

private:
    [[nodiscard]] bool PersistRecoveryState(std::wstring* error_message);

    ITaskbarMutationController* controller_ = nullptr;
    IRecoveryStateStore* recovery_store_ = nullptr;
    IFixedEntryReadinessGate* readiness_gate_ = nullptr;
    std::optional<WindowIdentity> main_entry_;
    std::vector<TaskbarMutationState> removed_windows_;
    std::size_t last_window_count_ = 0;
};

}  // namespace ctm
