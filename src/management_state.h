#pragma once

#include "app_config.h"

#include <string_view>

namespace ctm {

enum class ManagementState {
    Initializing,
    PreparingGroup,
    WaitingForTabProvider,
    Managing,
    PausedByUser,
    PausedByConflict,
    PausedByError,
    RecoveryRequired,
};

class ManagementStateMachine final {
public:
    explicit ManagementStateMachine(
        TabProvider provider = TabProvider::BuiltIn) noexcept
        : provider_(provider) {}

    [[nodiscard]] ManagementState state() const noexcept {
        return state_;
    }
    [[nodiscard]] TabProvider provider() const noexcept {
        return provider_;
    }
    [[nodiscard]] bool preparing() const noexcept {
        return state_ == ManagementState::PreparingGroup;
    }
    [[nodiscard]] bool managing() const noexcept {
        return state_ == ManagementState::Managing;
    }
    [[nodiscard]] bool waiting_for_tab_provider() const noexcept {
        return state_ == ManagementState::WaitingForTabProvider;
    }
    [[nodiscard]] bool waiting_for_windowtabs() const noexcept {
        return provider_ == TabProvider::WindowTabs &&
               waiting_for_tab_provider();
    }
    [[nodiscard]] bool paused_by_conflict() const noexcept {
        return state_ == ManagementState::PausedByConflict;
    }
    [[nodiscard]] bool recovery_required() const noexcept {
        return state_ == ManagementState::RecoveryRequired;
    }
    [[nodiscard]] bool can_pause() const noexcept;
    [[nodiscard]] bool can_resume() const noexcept;

    void CompleteInitialization(
        bool provider_available,
        bool conflict_present = false) noexcept;
    void PreparationCompleted(
        bool succeeded,
        bool restoration_succeeded = true) noexcept;
    [[nodiscard]] bool TabProviderBecameAvailable() noexcept;
    void TabProviderBecameUnavailable(
        bool restoration_succeeded) noexcept;
    void ConflictDetected(bool restoration_succeeded) noexcept;
    [[nodiscard]] bool ConflictCleared() noexcept;
    void PauseByUser(bool restoration_succeeded) noexcept;
    [[nodiscard]] bool ResumeRequested(
        bool provider_available,
        bool conflict_present = false) noexcept;
    void OperationFailed(bool restoration_succeeded) noexcept;
    void RequireRecovery() noexcept;
    void ExplicitRestoreCompleted(bool succeeded) noexcept;

    // Compatibility names used by the WindowTabs provider backend.
    [[nodiscard]] bool WindowTabsBecameAvailable() noexcept {
        return TabProviderBecameAvailable();
    }
    void WindowTabsBecameUnavailable(
        const bool restoration_succeeded) noexcept {
        TabProviderBecameUnavailable(restoration_succeeded);
    }

private:
    [[nodiscard]] ManagementState ReadyState(
        bool provider_available,
        bool conflict_present) const noexcept;

    TabProvider provider_ = TabProvider::BuiltIn;
    ManagementState state_ = ManagementState::Initializing;
};

[[nodiscard]] std::wstring_view ManagementStateLogName(
    ManagementState state) noexcept;

}  // namespace ctm
