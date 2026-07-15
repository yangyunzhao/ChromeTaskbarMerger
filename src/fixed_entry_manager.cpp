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
              main_entry_->class_name)));

    std::size_t index = 0;
    while (index < removed_windows_.size()) {
        TaskbarMutationState& state = removed_windows_[index];
        if (!state.NeedsRestore()) {
            removed_windows_.erase(removed_windows_.begin() + index);
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
            removed_windows_.erase(removed_windows_.begin() + index);
            continue;
        }

        report.succeeded = false;
        ++index;
    }

    if (!report.succeeded) {
        report.message =
            L"Restoration reconciliation failed; no new windows were removed.";
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
            removed_windows_.push_back(std::move(state));
        }
        if (!result.succeeded || !restoration_required) {
            report.succeeded = false;
        }
    }

    report.message = report.succeeded
                         ? L"The fixed taskbar entry is synchronized."
                         : L"One or more taskbar removals failed.";
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

    std::size_t index = 0;
    while (index < removed_windows_.size()) {
        TaskbarMutationState& state = removed_windows_[index];
        if (!state.NeedsRestore()) {
            removed_windows_.erase(removed_windows_.begin() + index);
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
            removed_windows_.erase(removed_windows_.begin() + index);
            continue;
        }

        report.succeeded = false;
        ++index;
    }

    report.message = report.succeeded
                         ? L"All taskbar entries changed by this session were restored."
                         : L"One or more taskbar entries still need restoration.";
    return report;
}

}  // namespace ctm
