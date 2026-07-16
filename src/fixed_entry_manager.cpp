#include "fixed_entry_manager.h"

#include <algorithm>
#include <cstdint>
#include <tuple>

namespace ctm {
namespace {

[[nodiscard]] WindowIdentity MakeIdentity(
    const ChromeWindowSnapshot& snapshot) {
    return {
        .hwnd = snapshot.hwnd,
        .process_id = snapshot.process_id,
        .thread_id = snapshot.thread_id,
        .process_creation_time = snapshot.process_creation_time,
        .class_name = snapshot.class_name,
    };
}

[[nodiscard]] bool IdentityMatchesSnapshot(
    const WindowIdentity& identity,
    const ChromeWindowSnapshot& snapshot) noexcept {
    return identity.hwnd == snapshot.hwnd &&
           WindowIdentityValuesMatch(
               identity,
               snapshot.process_id,
               snapshot.thread_id,
               snapshot.process_creation_time,
               snapshot.class_name);
}

[[nodiscard]] const ChromeWindowSnapshot* FindMatchingWindow(
    const std::span<const ChromeWindowSnapshot> windows,
    const WindowIdentity& identity) noexcept {
    const auto match = std::find_if(
        windows.begin(),
        windows.end(),
        [&identity](const ChromeWindowSnapshot& snapshot) {
            return IdentityMatchesSnapshot(identity, snapshot);
        });
    return match == windows.end() ? nullptr : &*match;
}

[[nodiscard]] const ChromeWindowSnapshot* SelectStableMainEntry(
    const std::span<const ChromeWindowSnapshot> windows,
    const HWND foreground_window) noexcept {
    const auto foreground = std::find_if(
        windows.begin(),
        windows.end(),
        [foreground_window](const ChromeWindowSnapshot& snapshot) {
            return snapshot.hwnd == foreground_window;
        });
    if (foreground != windows.end()) {
        return &*foreground;
    }

    const auto stable = std::min_element(
        windows.begin(),
        windows.end(),
        [](const ChromeWindowSnapshot& left,
           const ChromeWindowSnapshot& right) {
            return std::tuple{
                       reinterpret_cast<std::uintptr_t>(left.hwnd),
                       left.process_id,
                       left.thread_id,
                       left.class_name} <
                   std::tuple{
                       reinterpret_cast<std::uintptr_t>(right.hwnd),
                       right.process_id,
                       right.thread_id,
                       right.class_name};
        });
    return stable == windows.end() ? nullptr : &*stable;
}

[[nodiscard]] bool StateMatchesSnapshot(
    const TaskbarMutationState& state,
    const ChromeWindowSnapshot& snapshot) noexcept {
    return IdentityMatchesSnapshot(state.identity, snapshot);
}

[[nodiscard]] bool IsMainEntry(
    const std::optional<WindowIdentity>& main_entry,
    const ChromeWindowSnapshot& snapshot) noexcept {
    return main_entry.has_value() &&
           IdentityMatchesSnapshot(*main_entry, snapshot);
}

}  // namespace

FixedEntryReport FixedEntryManager::Synchronize(
    const std::span<const ChromeWindowSnapshot> windows,
    const HWND foreground_window) {
    FixedEntryReport report;
    report.manageable_window_count = windows.size();
    last_window_count_ = windows.size();
    if (controller_ == nullptr) {
        report.succeeded = false;
        report.message = L"The taskbar controller is unavailable.";
        return report;
    }

    const std::optional<WindowIdentity> previous_main = main_entry_;
    if (windows.empty()) {
        main_entry_.reset();
    } else if (!main_entry_.has_value() ||
               FindMatchingWindow(windows, *main_entry_) == nullptr) {
        const ChromeWindowSnapshot* const selected =
            SelectStableMainEntry(windows, foreground_window);
        if (selected != nullptr) {
            main_entry_ = MakeIdentity(*selected);
        }
    }

    report.main_entry = main_entry_;
    report.main_entry_changed =
        previous_main.has_value() != main_entry_.has_value() ||
        (previous_main.has_value() && main_entry_.has_value() &&
         (previous_main->hwnd != main_entry_->hwnd ||
          !WindowIdentityValuesMatch(
              *previous_main,
              main_entry_->process_id,
              main_entry_->thread_id,
              main_entry_->process_creation_time,
              main_entry_->class_name)));

    bool recovery_state_changed = false;
    std::size_t index = 0;
    while (index < removed_windows_.size()) {
        TaskbarMutationState& state = removed_windows_[index];
        if (!state.NeedsRestore()) {
            if (readiness_gate_ != nullptr) {
                readiness_gate_->RecoveryIntentCleared(state.identity);
            }
            removed_windows_.erase(removed_windows_.begin() + index);
            recovery_state_changed = true;
            continue;
        }

        const ChromeWindowSnapshot* const current =
            FindMatchingWindow(windows, state.identity);
        const bool became_main =
            current != nullptr && IsMainEntry(main_entry_, *current);
        if (current != nullptr && !became_main) {
            ++report.already_removed_count;
            ++index;
            continue;
        }

        const WindowIdentity identity = state.identity;
        TaskbarOperationResult result = controller_->RestoreWindow(&state);
        report.operations.push_back({
            .kind = FixedEntryOperationKind::Restore,
            .identity = identity,
            .result = result,
        });
        if (result.succeeded && !state.NeedsRestore()) {
            if (readiness_gate_ != nullptr) {
                readiness_gate_->RecoveryIntentCleared(identity);
            }
            removed_windows_.erase(removed_windows_.begin() + index);
            recovery_state_changed = true;
            continue;
        }

        report.succeeded = false;
        ++index;
    }

    if (recovery_state_changed) {
        std::wstring persistence_error;
        if (!PersistRecoveryState(&persistence_error)) {
            report.succeeded = false;
            report.persistence_error = std::move(persistence_error);
        }
    }

    if (!report.succeeded) {
        report.message =
            report.persistence_error.empty()
                ? L"Restoration reconciliation failed; no new windows were removed."
                : L"Recovery state persistence failed; no new windows were removed.";
        return report;
    }

    for (const ChromeWindowSnapshot& snapshot : windows) {
        if (IsMainEntry(main_entry_, snapshot)) {
            continue;
        }

        const bool already_removed = std::any_of(
            removed_windows_.begin(),
            removed_windows_.end(),
            [&snapshot](const TaskbarMutationState& state) {
                return state.NeedsRestore() &&
                       StateMatchesSnapshot(state, snapshot);
            });
        if (already_removed) {
            continue;
        }

        TaskbarMutationState planned_state;
        planned_state.method = TaskbarMethod::TaskbarList;
        planned_state.identity = MakeIdentity(snapshot);
        planned_state.modification_applied = true;
        removed_windows_.push_back(planned_state);

        std::wstring write_ahead_error;
        if (!PersistRecoveryState(&write_ahead_error)) {
            removed_windows_.pop_back();
            TaskbarOperationResult blocked;
            blocked.win32_error = ERROR_WRITE_FAULT;
            blocked.message =
                L"The recovery write-ahead failed; DeleteTab was not called.";
            report.operations.push_back({
                .kind = FixedEntryOperationKind::Remove,
                .identity = planned_state.identity,
                .result = std::move(blocked),
            });
            report.succeeded = false;
            report.persistence_error = std::move(write_ahead_error);
            break;
        }

        if (readiness_gate_ != nullptr) {
            std::wstring readiness_error;
            if (!readiness_gate_->ConfirmReadyAfterRecoveryWrite(
                    planned_state.identity, &readiness_error)) {
                removed_windows_.pop_back();
                std::wstring rollback_error;
                if (!PersistRecoveryState(&rollback_error)) {
                    removed_windows_.push_back(planned_state);
                    report.persistence_error = std::move(rollback_error);
                } else {
                    readiness_gate_->RecoveryIntentCleared(
                        planned_state.identity);
                }

                TaskbarOperationResult blocked;
                blocked.win32_error = ERROR_NOT_READY;
                blocked.message =
                    L"The internal-tab readiness gate rejected DeleteTab.";
                report.operations.push_back({
                    .kind = FixedEntryOperationKind::Remove,
                    .identity = planned_state.identity,
                    .result = std::move(blocked),
                });
                report.succeeded = false;
                report.readiness_error = std::move(readiness_error);
                break;
            }
        }

        TaskbarMutationState state;
        TaskbarOperationResult result = controller_->RemoveWindow(
            snapshot, TaskbarMethod::TaskbarList, &state);
        const WindowIdentity identity =
            state.identity.hwnd != nullptr ? state.identity
                                           : MakeIdentity(snapshot);
        report.operations.push_back({
            .kind = FixedEntryOperationKind::Remove,
            .identity = identity,
            .result = result,
        });

        const bool restoration_required = state.NeedsRestore();
        if (restoration_required) {
            removed_windows_.back() = std::move(state);
        } else {
            removed_windows_.pop_back();
        }

        std::wstring persistence_error;
        if (!PersistRecoveryState(&persistence_error)) {
            report.succeeded = false;
            report.persistence_error = std::move(persistence_error);
            break;
        }
        if (!restoration_required && readiness_gate_ != nullptr) {
            readiness_gate_->RecoveryIntentCleared(identity);
        }
        if (!result.succeeded || !restoration_required) {
            report.succeeded = false;
        }
    }

    if (report.succeeded) {
        report.message = L"The fixed taskbar entry is synchronized.";
    } else if (!report.persistence_error.empty()) {
        report.message =
            L"Recovery state persistence failed; management must pause.";
    } else if (!report.readiness_error.empty()) {
        report.message =
            L"The internal-tab readiness gate blocked taskbar removal.";
    } else {
        report.message = L"One or more taskbar removals failed.";
    }
    return report;
}

FixedEntryReport FixedEntryManager::RestoreAll() {
    FixedEntryReport report;
    report.manageable_window_count = last_window_count_;
    report.main_entry = main_entry_;
    if (controller_ == nullptr) {
        report.succeeded = false;
        report.message = L"The taskbar controller is unavailable.";
        return report;
    }

    bool recovery_state_changed = false;
    std::size_t index = 0;
    while (index < removed_windows_.size()) {
        TaskbarMutationState& state = removed_windows_[index];
        if (!state.NeedsRestore()) {
            if (readiness_gate_ != nullptr) {
                readiness_gate_->RecoveryIntentCleared(state.identity);
            }
            removed_windows_.erase(removed_windows_.begin() + index);
            recovery_state_changed = true;
            continue;
        }

        const WindowIdentity identity = state.identity;
        TaskbarOperationResult result = controller_->RestoreWindow(&state);
        report.operations.push_back({
            .kind = FixedEntryOperationKind::Restore,
            .identity = identity,
            .result = result,
        });
        if (result.succeeded && !state.NeedsRestore()) {
            if (readiness_gate_ != nullptr) {
                readiness_gate_->RecoveryIntentCleared(identity);
            }
            removed_windows_.erase(removed_windows_.begin() + index);
            recovery_state_changed = true;
            continue;
        }

        report.succeeded = false;
        ++index;
    }

    if (recovery_state_changed) {
        std::wstring persistence_error;
        if (!PersistRecoveryState(&persistence_error)) {
            report.succeeded = false;
            report.persistence_error = std::move(persistence_error);
        }
    }

    if (report.succeeded) {
        report.message =
            L"All taskbar entries changed by this session were restored.";
    } else if (!report.persistence_error.empty()) {
        report.message =
            L"Taskbar entries were processed, but recovery state could not be saved.";
    } else {
        report.message = L"One or more taskbar entries still need restoration.";
    }
    return report;
}

bool FixedEntryManager::AdoptRecoveryStates(
    std::vector<TaskbarMutationState> states,
    std::wstring* const error_message) {
    if (!removed_windows_.empty()) {
        if (error_message != nullptr) {
            *error_message = L"In-memory recovery state is not empty.";
        }
        return false;
    }
    for (const TaskbarMutationState& state : states) {
        if (!state.NeedsRestore() || state.identity.hwnd == nullptr ||
            state.identity.process_id == 0 || state.identity.thread_id == 0 ||
            state.identity.process_creation_time == 0 ||
            state.identity.class_name.empty()) {
            if (error_message != nullptr) {
                *error_message = L"A persisted recovery record is incomplete.";
            }
            return false;
        }
    }
    removed_windows_ = std::move(states);
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool FixedEntryManager::ResetAfterTaskbarRecreation(
    std::wstring* const error_message) {
    if (readiness_gate_ != nullptr) {
        for (const TaskbarMutationState& state : removed_windows_) {
            readiness_gate_->RecoveryIntentCleared(state.identity);
        }
    }
    removed_windows_.clear();
    return PersistRecoveryState(error_message);
}

bool FixedEntryManager::PersistRecoveryState(
    std::wstring* const error_message) {
    if (recovery_store_ == nullptr) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }
    return recovery_store_->Save(removed_windows_, error_message);
}

}  // namespace ctm
