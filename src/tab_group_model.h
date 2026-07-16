#pragma once

#include "window_identity.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ctm {

struct TabGroupCandidate {
    WindowIdentity identity;
    std::wstring title;
};

struct TabReachabilityState {
    bool tab_created = false;
    bool activation_path_verified = false;
    bool recovery_intent_persisted = false;
};

struct TabGroupMember {
    WindowIdentity identity;
    std::wstring title;
    TabReachabilityState reachability;
};

struct TabGroupSyncReport {
    std::size_t added_count = 0;
    std::size_t removed_count = 0;
    std::size_t replaced_identity_count = 0;
    std::size_t updated_title_count = 0;
    std::size_t ignored_candidate_count = 0;
    bool active_changed = false;
};

class TabGroupModel final {
public:
    [[nodiscard]] TabGroupSyncReport Synchronize(
        std::span<const TabGroupCandidate> candidates,
        const std::optional<WindowIdentity>& preferred_active = std::nullopt);

    [[nodiscard]] bool MarkTabCreated(
        const WindowIdentity& identity,
        bool created) noexcept;
    [[nodiscard]] bool MarkActivationPathVerified(
        const WindowIdentity& identity,
        bool verified) noexcept;
    [[nodiscard]] bool MarkRecoveryIntentPersisted(
        const WindowIdentity& identity,
        bool persisted) noexcept;
    void SetTabStripHealthy(bool healthy) noexcept {
        tab_strip_healthy_ = healthy;
    }

    [[nodiscard]] bool CanActivate(
        const WindowIdentity& identity) const noexcept;
    [[nodiscard]] bool CanRemoveFromTaskbar(
        const WindowIdentity& identity) const noexcept;
    [[nodiscard]] bool SetActive(
        const WindowIdentity& identity) noexcept;

    [[nodiscard]] const TabGroupMember* FindMember(
        const WindowIdentity& identity) const noexcept;
    [[nodiscard]] std::span<const TabGroupMember> members() const noexcept {
        return members_;
    }
    [[nodiscard]] const std::optional<WindowIdentity>& active_identity()
        const noexcept {
        return active_identity_;
    }
    [[nodiscard]] bool tab_strip_healthy() const noexcept {
        return tab_strip_healthy_;
    }

private:
    [[nodiscard]] TabGroupMember* FindMutableMember(
        const WindowIdentity& identity) noexcept;

    std::vector<TabGroupMember> members_;
    std::optional<WindowIdentity> active_identity_;
    bool tab_strip_healthy_ = false;
};

}  // namespace ctm
