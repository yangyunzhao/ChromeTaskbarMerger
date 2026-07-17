#pragma once

#include "recovery_journal.h"
#include "window_identity.h"

#include <Windows.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

struct GroupDisplayRecoveryState {
    std::wstring device_name;
    RECT monitor_bounds{};
    RECT work_area{};
};

struct GroupMemberRecoveryState {
    WindowIdentity identity;
    WINDOWPLACEMENT original_placement{};
    RECT original_rectangle{};
    GroupDisplayRecoveryState display;
    bool tab_created = false;
    bool layout_restore_required = false;
    bool layout_restore_completed = false;

    [[nodiscard]] bool NeedsLayoutRestore() const noexcept {
        return layout_restore_required && !layout_restore_completed;
    }
};

struct GroupRecoveryState {
    bool session_active = false;
    bool tab_strip_created = false;
    std::vector<GroupMemberRecoveryState> members;
    std::vector<TaskbarMutationState> taskbar_states;

    [[nodiscard]] bool HasObligations() const noexcept;
};

struct GroupRecoveryParseResult {
    bool succeeded = false;
    GroupRecoveryState state;
    std::wstring error_message;
};

struct GroupRecoveryLoadResult {
    bool succeeded = false;
    bool file_found = false;
    GroupRecoveryState state;
    std::wstring error_message;
};

[[nodiscard]] std::string SerializeGroupRecoveryState(
    const GroupRecoveryState& state);
[[nodiscard]] GroupRecoveryParseResult ParseGroupRecoveryState(
    std::string_view serialized);

class IGroupRecoveryPersistence {
public:
    virtual ~IGroupRecoveryPersistence() = default;

    [[nodiscard]] virtual bool SaveAtomically(
        const std::filesystem::path& path,
        std::string_view serialized,
        std::wstring* error_message) = 0;
};

class GroupRecoveryJournal final : public IRecoveryStateStore {
public:
    explicit GroupRecoveryJournal(
        std::filesystem::path path,
        IGroupRecoveryPersistence* persistence = nullptr)
        : path_(std::move(path)), persistence_(persistence) {}

    [[nodiscard]] GroupRecoveryLoadResult Load() const;
    [[nodiscard]] bool Adopt(
        GroupRecoveryState state,
        std::wstring* error_message);
    [[nodiscard]] bool BeginSession(
        std::span<const WindowIdentity> identities,
        std::wstring* error_message);
    [[nodiscard]] bool EnsureMembers(
        std::span<const WindowIdentity> identities,
        std::wstring* error_message);
    [[nodiscard]] bool MarkTabStripCreated(
        bool created,
        std::wstring* error_message);
    [[nodiscard]] bool MarkTabsCreated(
        std::span<const WindowIdentity> identities,
        bool created,
        std::wstring* error_message);
    [[nodiscard]] bool PlanLayoutMutation(
        std::span<const WindowIdentity> identities,
        std::wstring* error_message);
    [[nodiscard]] bool MarkLayoutRestored(
        const WindowIdentity& identity,
        std::wstring* error_message);
    [[nodiscard]] bool Clear(std::wstring* error_message);

    [[nodiscard]] bool Save(
        std::span<const TaskbarMutationState> states,
        std::wstring* error_message) override;

    [[nodiscard]] const GroupRecoveryState& state() const noexcept {
        return state_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    [[nodiscard]] bool PersistCandidate(
        GroupRecoveryState candidate,
        std::wstring* error_message);

    std::filesystem::path path_;
    IGroupRecoveryPersistence* persistence_ = nullptr;
    GroupRecoveryState state_;
};

enum class GroupRecoveryTargetStatus {
    Valid,
    Missing,
    IdentityMismatch,
    DisplayMismatch,
    Unavailable,
};

struct GroupRecoveryTargetCheck {
    GroupRecoveryTargetStatus status = GroupRecoveryTargetStatus::Unavailable;
    DWORD win32_error = ERROR_SUCCESS;
    std::wstring message;
};

class IGroupRecoveryWindowGateway {
public:
    virtual ~IGroupRecoveryWindowGateway() = default;

    [[nodiscard]] virtual GroupRecoveryTargetCheck Check(
        const GroupMemberRecoveryState& member) = 0;
    [[nodiscard]] virtual GroupRecoveryTargetCheck Restore(
        const GroupMemberRecoveryState& member) = 0;
};

class Win32GroupRecoveryWindowGateway final
    : public IGroupRecoveryWindowGateway {
public:
    [[nodiscard]] GroupRecoveryTargetCheck Check(
        const GroupMemberRecoveryState& member) override;
    [[nodiscard]] GroupRecoveryTargetCheck Restore(
        const GroupMemberRecoveryState& member) override;
};

struct GroupLayoutRecoveryOperation {
    WindowIdentity identity;
    GroupRecoveryTargetStatus status = GroupRecoveryTargetStatus::Unavailable;
    bool succeeded = false;
    bool safely_skipped = false;
    DWORD win32_error = ERROR_SUCCESS;
    std::wstring message;
};

struct GroupLayoutRecoveryReport {
    bool succeeded = true;
    std::size_t restored_count = 0;
    std::size_t safely_skipped_count = 0;
    std::vector<GroupLayoutRecoveryOperation> operations;
    std::wstring persistence_error;
};

[[nodiscard]] GroupLayoutRecoveryReport RestorePersistedGroupLayouts(
    GroupRecoveryJournal* journal,
    IGroupRecoveryWindowGateway* gateway);

}  // namespace ctm
