#include "management_state.h"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

void TestWindowTabsLifecycleAutomaticallyResumes() {
    ctm::ManagementStateMachine state;
    Expect(state.state() == ctm::ManagementState::Initializing,
           "the state machine should begin in the initializing state");

    state.CompleteInitialization(false);
    Expect(state.waiting_for_windowtabs() && state.can_pause(),
           "a missing prerequisite should enter an active waiting state");
    Expect(state.WindowTabsBecameAvailable() && state.managing(),
           "WindowTabs appearing should automatically start management");

    state.WindowTabsBecameUnavailable(true);
    Expect(state.waiting_for_windowtabs(),
           "a clean prerequisite loss should return to waiting");
    Expect(state.WindowTabsBecameAvailable() && state.managing(),
           "WindowTabs restarting should automatically resume management");
}

void TestUserPauseIsStickyAcrossWindowTabsChanges() {
    ctm::ManagementStateMachine state;
    state.CompleteInitialization(true);
    state.PauseByUser(true);
    Expect(state.state() == ctm::ManagementState::PausedByUser &&
               state.can_resume(),
           "a successful user pause should be explicit and resumable");
    Expect(!state.WindowTabsBecameAvailable() &&
               state.state() == ctm::ManagementState::PausedByUser,
           "WindowTabs activity must not override an explicit user pause");
    state.WindowTabsBecameUnavailable(false);
    Expect(state.state() == ctm::ManagementState::PausedByUser,
           "WindowTabs loss must not override an explicit user pause");

    Expect(state.ResumeRequested(false) &&
               state.waiting_for_windowtabs(),
           "resuming without WindowTabs should enter automatic waiting");
    state.PauseByUser(true);
    Expect(state.state() == ctm::ManagementState::PausedByUser,
           "the user should also be able to cancel prerequisite waiting");
}

void TestFailuresCannotBypassRecovery() {
    ctm::ManagementStateMachine state;
    state.CompleteInitialization(true);
    state.WindowTabsBecameUnavailable(false);
    Expect(state.recovery_required() && !state.can_resume(),
           "a failed restoration should require recovery and block resume");
    Expect(!state.ResumeRequested(true) && state.recovery_required(),
           "a recovery-required state must reject ordinary resume requests");
    state.CompleteInitialization(true);
    state.WindowTabsBecameUnavailable(true);
    state.PauseByUser(true);
    state.OperationFailed(true);
    Expect(state.recovery_required(),
           "unrelated lifecycle events must not downgrade recovery-required "
           "state");

    state.ExplicitRestoreCompleted(true);
    Expect(state.state() == ctm::ManagementState::PausedByUser &&
               state.ResumeRequested(true) && state.managing(),
           "a successful explicit restore should unlock management");
}

void TestOperationalFailureIsDistinctFromUserPause() {
    ctm::ManagementStateMachine state;
    state.CompleteInitialization(true);
    state.OperationFailed(true);
    Expect(state.state() == ctm::ManagementState::PausedByError &&
               state.can_resume(),
           "a safely restored runtime failure should be an error pause");

    state.OperationFailed(false);
    Expect(state.recovery_required(),
           "a runtime failure with incomplete restoration should require "
           "recovery");
}

}  // namespace

int main() {
    TestWindowTabsLifecycleAutomaticallyResumes();
    TestUserPauseIsStickyAcrossWindowTabsChanges();
    TestFailuresCannotBypassRecovery();
    TestOperationalFailureIsDistinctFromUserPause();

    if (failures != 0) {
        std::cerr << failures << " management-state test(s) failed.\n";
        return 1;
    }
    std::cout << "All management-state tests passed.\n";
    return 0;
}
