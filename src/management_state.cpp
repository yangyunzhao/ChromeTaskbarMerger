#include "management_state.h"

namespace ctm {

bool ManagementStateMachine::can_pause() const noexcept {
    return preparing() || managing() || waiting_for_tab_provider() ||
           paused_by_conflict();
}

bool ManagementStateMachine::can_resume() const noexcept {
    return state_ == ManagementState::PausedByUser ||
           state_ == ManagementState::PausedByError;
}

ManagementState ManagementStateMachine::ReadyState(
    const bool provider_available,
    const bool conflict_present) const noexcept {
    if (provider_ == TabProvider::BuiltIn) {
        return conflict_present ? ManagementState::PausedByConflict
                                : ManagementState::PreparingGroup;
    }
    return provider_available ? ManagementState::PreparingGroup
                              : ManagementState::WaitingForTabProvider;
}

void ManagementStateMachine::CompleteInitialization(
    const bool provider_available,
    const bool conflict_present) noexcept {
    if (state_ != ManagementState::Initializing) {
        return;
    }
    state_ = ReadyState(provider_available, conflict_present);
}

void ManagementStateMachine::PreparationCompleted(
    const bool succeeded,
    const bool restoration_succeeded) noexcept {
    if (!preparing()) {
        return;
    }
    if (succeeded) {
        state_ = ManagementState::Managing;
    } else {
        state_ = restoration_succeeded ? ManagementState::PausedByError
                                       : ManagementState::RecoveryRequired;
    }
}

bool ManagementStateMachine::TabProviderBecameAvailable() noexcept {
    if (provider_ != TabProvider::WindowTabs ||
        !waiting_for_tab_provider()) {
        return false;
    }
    state_ = ManagementState::PreparingGroup;
    return true;
}

void ManagementStateMachine::TabProviderBecameUnavailable(
    const bool restoration_succeeded) noexcept {
    if (provider_ != TabProvider::WindowTabs ||
        (!managing() && !preparing())) {
        return;
    }
    state_ = restoration_succeeded
                 ? ManagementState::WaitingForTabProvider
                 : ManagementState::RecoveryRequired;
}

void ManagementStateMachine::ConflictDetected(
    const bool restoration_succeeded) noexcept {
    if (provider_ != TabProvider::BuiltIn ||
        (!managing() && !preparing())) {
        return;
    }
    state_ = restoration_succeeded ? ManagementState::PausedByConflict
                                   : ManagementState::RecoveryRequired;
}

bool ManagementStateMachine::ConflictCleared() noexcept {
    if (provider_ != TabProvider::BuiltIn || !paused_by_conflict()) {
        return false;
    }
    state_ = ManagementState::PreparingGroup;
    return true;
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
    const bool provider_available,
    const bool conflict_present) noexcept {
    if (!can_resume()) {
        return false;
    }
    state_ = ReadyState(provider_available, conflict_present);
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
        case ManagementState::PreparingGroup:
            return L"preparing-group";
        case ManagementState::WaitingForTabProvider:
            return L"waiting-for-tab-provider";
        case ManagementState::Managing:
            return L"managing";
        case ManagementState::PausedByUser:
            return L"paused-by-user";
        case ManagementState::PausedByConflict:
            return L"paused-by-conflict";
        case ManagementState::PausedByError:
            return L"paused-by-error";
        case ManagementState::RecoveryRequired:
            return L"recovery-required";
    }
    return L"unknown";
}

}  // namespace ctm
