#pragma once

#include <string_view>

namespace ctm {

enum class ManagementState {
    Initializing,
    WaitingForWindowTabs,
    Managing,
    PausedByUser,
    PausedByError,
    RecoveryRequired,
};

class ManagementStateMachine final {
public:
    [[nodiscard]] ManagementState state() const noexcept {
        return state_;
    }
    [[nodiscard]] bool managing() const noexcept {
        return state_ == ManagementState::Managing;
    }
    [[nodiscard]] bool waiting_for_windowtabs() const noexcept {
        return state_ == ManagementState::WaitingForWindowTabs;
    }
    [[nodiscard]] bool recovery_required() const noexcept {
        return state_ == ManagementState::RecoveryRequired;
    }
    [[nodiscard]] bool can_pause() const noexcept;
    [[nodiscard]] bool can_resume() const noexcept;

    void CompleteInitialization(bool windowtabs_available) noexcept;
    [[nodiscard]] bool WindowTabsBecameAvailable() noexcept;
    void WindowTabsBecameUnavailable(bool restoration_succeeded) noexcept;
    void PauseByUser(bool restoration_succeeded) noexcept;
    [[nodiscard]] bool ResumeRequested(bool windowtabs_available) noexcept;
    void OperationFailed(bool restoration_succeeded) noexcept;
    void RequireRecovery() noexcept;
    void ExplicitRestoreCompleted(bool succeeded) noexcept;

private:
    ManagementState state_ = ManagementState::Initializing;
};

[[nodiscard]] std::wstring_view ManagementStateLogName(
    ManagementState state) noexcept;

}  // namespace ctm
