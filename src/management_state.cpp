#include "management_state.h"

namespace ctm {

bool ManagementStateMachine::can_pause() const noexcept {
    return managing() || waiting_for_windowtabs();
}

bool ManagementStateMachine::can_resume() const noexcept {
    return state_ == ManagementState::PausedByUser ||
           state_ == ManagementState::PausedByError;
}

void ManagementStateMachine::CompleteInitialization(
    const bool windowtabs_available) noexcept {
    if (state_ != ManagementState::Initializing) {
        return;
    }
    state_ = windowtabs_available ? ManagementState::Managing
                                  : ManagementState::WaitingForWindowTabs;
}

bool ManagementStateMachine::WindowTabsBecameAvailable() noexcept {
    if (!waiting_for_windowtabs()) {
        return false;
    }
    state_ = ManagementState::Managing;
    return true;
}

void ManagementStateMachine::WindowTabsBecameUnavailable(
    const bool restoration_succeeded) noexcept {
    if (!managing()) {
        return;
    }
    state_ = restoration_succeeded ? ManagementState::WaitingForWindowTabs
                                   : ManagementState::RecoveryRequired;
}

void ManagementStateMachine::PauseByUser(
    const bool restoration_succeeded) noexcept {
    if (!can_pause()) {
        return;
    }
    state_ = restoration_succeeded ? ManagementState::PausedByUser
                                   : ManagementState::RecoveryRequired;
}

bool ManagementStateMachine::ResumeRequested(
    const bool windowtabs_available) noexcept {
    if (!can_resume()) {
        return false;
    }
    state_ = windowtabs_available ? ManagementState::Managing
                                  : ManagementState::WaitingForWindowTabs;
    return true;
}

void ManagementStateMachine::OperationFailed(
    const bool restoration_succeeded) noexcept {
    if (recovery_required()) {
        return;
    }
    state_ = restoration_succeeded ? ManagementState::PausedByError
                                   : ManagementState::RecoveryRequired;
}

void ManagementStateMachine::RequireRecovery() noexcept {
    state_ = ManagementState::RecoveryRequired;
}

void ManagementStateMachine::ExplicitRestoreCompleted(
    const bool succeeded) noexcept {
    state_ = succeeded ? ManagementState::PausedByUser
                       : ManagementState::RecoveryRequired;
}

std::wstring_view ManagementStateLogName(
    const ManagementState state) noexcept {
    switch (state) {
        case ManagementState::Initializing:
            return L"initializing";
        case ManagementState::WaitingForWindowTabs:
            return L"waiting-for-windowtabs";
        case ManagementState::Managing:
            return L"managing";
        case ManagementState::PausedByUser:
            return L"paused-by-user";
        case ManagementState::PausedByError:
            return L"paused-by-error";
        case ManagementState::RecoveryRequired:
            return L"recovery-required";
    }
    return L"unknown";
}

}  // namespace ctm
