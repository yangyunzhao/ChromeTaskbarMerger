#include "chrome_window_registry.h"

#include <algorithm>
#include <utility>

namespace ctm {
namespace {

[[nodiscard]] bool OptionalIdentitiesMatch(
    const std::optional<WindowIdentity>& left,
    const std::optional<WindowIdentity>& right) noexcept {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() || WindowIdentitiesMatch(*left, *right);
}

[[nodiscard]] bool SnapshotMatchesIdentity(
    const ChromeWindowSnapshot& snapshot,
    const WindowIdentity& identity) noexcept {
    return WindowIdentitiesMatch(MakeWindowIdentity(snapshot), identity);
}

[[nodiscard]] bool ContainsHandle(const std::span<const HWND> handles,
                                  const HWND hwnd) noexcept {
    return std::find(handles.begin(), handles.end(), hwnd) != handles.end();
}

}  // namespace

WindowIdentity MakeWindowIdentity(const ChromeWindowSnapshot& snapshot) {
    return {
        .hwnd = snapshot.hwnd,
        .process_id = snapshot.process_id,
        .thread_id = snapshot.thread_id,
        .process_creation_time = snapshot.process_creation_time,
        .class_name = snapshot.class_name,
    };
}

ChromeWindowRegistry::ChromeWindowRegistry() noexcept
    : owner_thread_id_(GetCurrentThreadId()) {}

bool ChromeWindowRegistry::CheckThread(
    ChromeWindowRegistryReport* const report) const noexcept {
    if (GetCurrentThreadId() == owner_thread_id_) {
        return true;
    }
    if (report != nullptr) {
        report->succeeded = false;
        report->win32_error = ERROR_INVALID_THREAD_ID;
    }
    return false;
}

ChromeWindowRegistryReport ChromeWindowRegistry::Synchronize(
    const std::span<const ChromeWindowSnapshot> snapshots,
    const HWND foreground_window) {
    ChromeWindowRegistryReport report;
    if (!CheckThread(&report)) {
        return report;
    }

    const std::optional<WindowIdentity> previous_active = active_identity_;
    std::vector<ChromeWindowSnapshot> unique;
    unique.reserve(snapshots.size());
    for (const ChromeWindowSnapshot& snapshot : snapshots) {
        const WindowIdentity identity = MakeWindowIdentity(snapshot);
        const bool duplicate = std::any_of(
            unique.begin(),
            unique.end(),
            [&snapshot](const ChromeWindowSnapshot& existing) {
                return existing.hwnd == snapshot.hwnd;
            });
        if (!WindowIdentityIsComplete(identity) || duplicate) {
            ++report.ignored_snapshot_count;
            continue;
        }
        unique.push_back(snapshot);
    }

    std::vector<bool> consumed(unique.size(), false);
    std::vector<ChromeWindowSnapshot> synchronized;
    synchronized.reserve(unique.size());
    for (const ChromeWindowSnapshot& existing : windows_) {
        auto exact = unique.end();
        for (auto candidate = unique.begin(); candidate != unique.end();
             ++candidate) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique.begin(), candidate));
            if (!consumed[index] &&
                WindowIdentitiesMatch(
                    MakeWindowIdentity(existing),
                    MakeWindowIdentity(*candidate))) {
                exact = candidate;
                break;
            }
        }
        if (exact != unique.end()) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique.begin(), exact));
            consumed[index] = true;
            if (existing.title != exact->title) {
                ++report.updated_title_count;
            }
            synchronized.push_back(*exact);
            continue;
        }

        auto replacement = unique.end();
        for (auto candidate = unique.begin(); candidate != unique.end();
             ++candidate) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique.begin(), candidate));
            if (!consumed[index] && existing.hwnd == candidate->hwnd) {
                replacement = candidate;
                break;
            }
        }
        if (replacement != unique.end()) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique.begin(), replacement));
            consumed[index] = true;
            report.removed.push_back(existing);
            report.added.push_back(*replacement);
            synchronized.push_back(*replacement);
            ++report.removed_count;
            ++report.added_count;
            ++report.replaced_identity_count;
            continue;
        }

        report.removed.push_back(existing);
        ++report.removed_count;
    }

    for (std::size_t index = 0; index < unique.size(); ++index) {
        if (consumed[index]) {
            continue;
        }
        report.added.push_back(unique[index]);
        synchronized.push_back(unique[index]);
        ++report.added_count;
    }

    windows_ = std::move(synchronized);
    const auto foreground = std::find_if(
        windows_.begin(),
        windows_.end(),
        [foreground_window](const ChromeWindowSnapshot& snapshot) {
            return foreground_window != nullptr &&
                   snapshot.hwnd == foreground_window;
        });
    if (foreground != windows_.end()) {
        active_identity_ = MakeWindowIdentity(*foreground);
    } else if (previous_active.has_value()) {
        const auto retained = std::find_if(
            windows_.begin(),
            windows_.end(),
            [&previous_active](const ChromeWindowSnapshot& snapshot) {
                return SnapshotMatchesIdentity(snapshot, *previous_active);
            });
        if (retained != windows_.end()) {
            active_identity_ = previous_active;
        } else if (!windows_.empty()) {
            active_identity_ = MakeWindowIdentity(windows_.front());
        } else {
            active_identity_.reset();
        }
    } else if (!windows_.empty()) {
        active_identity_ = MakeWindowIdentity(windows_.front());
    } else {
        active_identity_.reset();
    }

    report.active_changed =
        !OptionalIdentitiesMatch(previous_active, active_identity_);
    return report;
}

ChromeWindowRegistryReport ChromeWindowRegistry::InvalidateHandles(
    const std::span<const HWND> destroyed_handles) {
    ChromeWindowRegistryReport report;
    if (!CheckThread(&report)) {
        return report;
    }
    if (destroyed_handles.empty()) {
        return report;
    }

    const std::optional<WindowIdentity> previous_active = active_identity_;
    std::vector<ChromeWindowSnapshot> retained;
    retained.reserve(windows_.size());
    for (const ChromeWindowSnapshot& snapshot : windows_) {
        if (ContainsHandle(destroyed_handles, snapshot.hwnd)) {
            report.removed.push_back(snapshot);
            ++report.removed_count;
        } else {
            retained.push_back(snapshot);
        }
    }
    windows_ = std::move(retained);
    if (previous_active.has_value()) {
        const auto active = std::find_if(
            windows_.begin(),
            windows_.end(),
            [&previous_active](const ChromeWindowSnapshot& snapshot) {
                return SnapshotMatchesIdentity(snapshot, *previous_active);
            });
        if (active != windows_.end()) {
            active_identity_ = previous_active;
        } else if (!windows_.empty()) {
            active_identity_ = MakeWindowIdentity(windows_.front());
        } else {
            active_identity_.reset();
        }
    }
    report.active_changed =
        !OptionalIdentitiesMatch(previous_active, active_identity_);
    return report;
}

}  // namespace ctm
