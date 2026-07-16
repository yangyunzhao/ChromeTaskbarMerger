#include "tab_group_model.h"

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

}  // namespace

TabGroupSyncReport TabGroupModel::Synchronize(
    const std::span<const TabGroupCandidate> candidates,
    const std::optional<WindowIdentity>& preferred_active) {
    TabGroupSyncReport report;
    const std::optional<WindowIdentity> previous_active = active_identity_;

    std::vector<TabGroupCandidate> unique_candidates;
    unique_candidates.reserve(candidates.size());
    for (const TabGroupCandidate& candidate : candidates) {
        const bool duplicate_handle = std::any_of(
            unique_candidates.begin(),
            unique_candidates.end(),
            [&candidate](const TabGroupCandidate& existing) {
                return existing.identity.hwnd == candidate.identity.hwnd;
            });
        if (!WindowIdentityIsComplete(candidate.identity) ||
            duplicate_handle) {
            ++report.ignored_candidate_count;
            continue;
        }
        unique_candidates.push_back(candidate);
    }

    std::vector<bool> consumed(unique_candidates.size(), false);
    std::vector<TabGroupMember> synchronized;
    synchronized.reserve(unique_candidates.size());

    for (const TabGroupMember& existing : members_) {
        auto exact = unique_candidates.end();
        for (auto candidate = unique_candidates.begin();
             candidate != unique_candidates.end();
             ++candidate) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique_candidates.begin(), candidate));
            if (!consumed[index] &&
                WindowIdentitiesMatch(existing.identity, candidate->identity)) {
                exact = candidate;
                break;
            }
        }

        if (exact != unique_candidates.end()) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique_candidates.begin(), exact));
            consumed[index] = true;
            TabGroupMember retained = existing;
            if (retained.title != exact->title) {
                retained.title = exact->title;
                ++report.updated_title_count;
            }
            synchronized.push_back(std::move(retained));
            continue;
        }

        auto replacement = unique_candidates.end();
        for (auto candidate = unique_candidates.begin();
             candidate != unique_candidates.end();
             ++candidate) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique_candidates.begin(), candidate));
            if (!consumed[index] &&
                existing.identity.hwnd == candidate->identity.hwnd) {
                replacement = candidate;
                break;
            }
        }

        if (replacement != unique_candidates.end()) {
            const std::size_t index = static_cast<std::size_t>(
                std::distance(unique_candidates.begin(), replacement));
            consumed[index] = true;
            synchronized.push_back({
                .identity = replacement->identity,
                .title = replacement->title,
            });
            ++report.replaced_identity_count;
            continue;
        }

        ++report.removed_count;
    }

    for (std::size_t index = 0; index < unique_candidates.size(); ++index) {
        if (consumed[index]) {
            continue;
        }
        synchronized.push_back({
            .identity = unique_candidates[index].identity,
            .title = unique_candidates[index].title,
        });
        ++report.added_count;
    }

    members_ = std::move(synchronized);
    if (previous_active.has_value() &&
        FindMember(*previous_active) != nullptr) {
        active_identity_ = previous_active;
    } else if (preferred_active.has_value() &&
               FindMember(*preferred_active) != nullptr) {
        active_identity_ = preferred_active;
    } else if (!members_.empty()) {
        active_identity_ = members_.front().identity;
    } else {
        active_identity_.reset();
    }

    report.active_changed =
        !OptionalIdentitiesMatch(previous_active, active_identity_);
    return report;
}

bool TabGroupModel::MarkTabCreated(const WindowIdentity& identity,
                                   const bool created) noexcept {
    TabGroupMember* const member = FindMutableMember(identity);
    if (member == nullptr) {
        return false;
    }
    member->reachability.tab_created = created;
    if (!created) {
        member->reachability.activation_path_verified = false;
    }
    return true;
}

bool TabGroupModel::MarkActivationPathVerified(
    const WindowIdentity& identity,
    const bool verified) noexcept {
    TabGroupMember* const member = FindMutableMember(identity);
    if (member == nullptr ||
        (verified && !member->reachability.tab_created)) {
        return false;
    }
    member->reachability.activation_path_verified = verified;
    return true;
}

bool TabGroupModel::MarkRecoveryIntentPersisted(
    const WindowIdentity& identity,
    const bool persisted) noexcept {
    TabGroupMember* const member = FindMutableMember(identity);
    if (member == nullptr) {
        return false;
    }
    member->reachability.recovery_intent_persisted = persisted;
    return true;
}

bool TabGroupModel::CanActivate(const WindowIdentity& identity) const noexcept {
    const TabGroupMember* const member = FindMember(identity);
    return member != nullptr && member->reachability.tab_created &&
           member->reachability.activation_path_verified;
}

bool TabGroupModel::CanRemoveFromTaskbar(
    const WindowIdentity& identity) const noexcept {
    const TabGroupMember* const member = FindMember(identity);
    return tab_strip_healthy_ && member != nullptr &&
           member->reachability.tab_created &&
           member->reachability.activation_path_verified &&
           member->reachability.recovery_intent_persisted;
}

bool TabGroupModel::SetActive(const WindowIdentity& identity) noexcept {
    if (!CanActivate(identity)) {
        return false;
    }
    active_identity_ = identity;
    return true;
}

const TabGroupMember* TabGroupModel::FindMember(
    const WindowIdentity& identity) const noexcept {
    const auto match = std::find_if(
        members_.begin(),
        members_.end(),
        [&identity](const TabGroupMember& member) {
            return WindowIdentitiesMatch(member.identity, identity);
        });
    return match == members_.end() ? nullptr : &*match;
}

TabGroupMember* TabGroupModel::FindMutableMember(
    const WindowIdentity& identity) noexcept {
    const auto match = std::find_if(
        members_.begin(),
        members_.end(),
        [&identity](const TabGroupMember& member) {
            return WindowIdentitiesMatch(member.identity, identity);
        });
    return match == members_.end() ? nullptr : &*match;
}

}  // namespace ctm
