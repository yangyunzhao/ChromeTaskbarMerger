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

void TestBuiltInProviderPreparesWithoutWindowTabs() {
    ctm::ManagementStateMachine state(ctm::TabProvider::BuiltIn);
    state.CompleteInitialization(false, false);
    Expect(state.preparing(),
           "built-in tabs should prepare without WindowTabs");
    state.PreparationCompleted(true);
    Expect(state.managing(),
           "successful built-in preparation should enter management");
}

void TestBuiltInProviderConflictIsSafeAndReversible() {
    ctm::ManagementStateMachine state(ctm::TabProvider::BuiltIn);
    state.CompleteInitialization(false, true);
    Expect(state.paused_by_conflict() && state.can_pause(),
           "WindowTabs should produce a safe conflict only in built-in mode");
    Expect(state.ConflictCleared() && state.preparing(),
           "clearing the external conflict should request preparation");
    state.PreparationCompleted(true);
    state.ConflictDetected(true);
    Expect(state.paused_by_conflict(),
           "a runtime conflict should restore before pausing");
}

void TestWindowTabsProviderWaitsAndAutomaticallyPrepares() {
    ctm::ManagementStateMachine state(ctm::TabProvider::WindowTabs);
    state.CompleteInitialization(false);
    Expect(state.waiting_for_tab_provider() &&
               state.waiting_for_windowtabs() && state.can_pause(),
           "the selected WindowTabs provider should wait while unavailable");
    Expect(state.TabProviderBecameAvailable() && state.preparing(),
           "WindowTabs appearing should request preparation");
    state.PreparationCompleted(true);
    Expect(state.managing(),
           "successful WindowTabs preparation should enter management");
    state.TabProviderBecameUnavailable(true);
    Expect(state.waiting_for_tab_provider(),
           "clean provider loss should return to waiting");
}

void TestUserPauseIsStickyAcrossProviderAndConflictChanges() {
    ctm::ManagementStateMachine windowtabs(ctm::TabProvider::WindowTabs);
    windowtabs.CompleteInitialization(true);
    windowtabs.PreparationCompleted(true);
    windowtabs.PauseByUser(true);
    Expect(windowtabs.state() == ctm::ManagementState::PausedByUser &&
               !windowtabs.TabProviderBecameAvailable(),
           "provider activity must not override a user pause");
    windowtabs.TabProviderBecameUnavailable(false);
    Expect(windowtabs.state() == ctm::ManagementState::PausedByUser,
           "provider loss must not override a user pause");

    ctm::ManagementStateMachine builtin(ctm::TabProvider::BuiltIn);
    builtin.CompleteInitialization(false, false);
    builtin.PreparationCompleted(true);
    builtin.PauseByUser(true);
    builtin.ConflictDetected(false);
    Expect(builtin.state() == ctm::ManagementState::PausedByUser,
           "a conflict event must not override a built-in user pause");
}

void TestResumeUsesTheSelectedProviderRules() {
    ctm::ManagementStateMachine builtin(ctm::TabProvider::BuiltIn);
    builtin.CompleteInitialization(false, false);
    builtin.PauseByUser(true);
    Expect(builtin.ResumeRequested(false, false) && builtin.preparing(),
           "built-in resume should not require WindowTabs");

    ctm::ManagementStateMachine windowtabs(ctm::TabProvider::WindowTabs);
    windowtabs.CompleteInitialization(false);
    windowtabs.PauseByUser(true);
    Expect(windowtabs.ResumeRequested(false) &&
               windowtabs.waiting_for_tab_provider(),
           "WindowTabs resume should wait when its provider is unavailable");
}

void TestFailuresCannotBypassRecovery() {
    ctm::ManagementStateMachine state(ctm::TabProvider::BuiltIn);
    state.CompleteInitialization(false, false);
    state.PreparationCompleted(true);
    state.ConflictDetected(false);
    Expect(state.recovery_required() && !state.can_resume(),
           "failed conflict restoration should require recovery");
    Expect(!state.ResumeRequested(true, false) && state.recovery_required(),
           "ordinary resume must not bypass required recovery");
    state.ExplicitRestoreCompleted(true);
    Expect(state.state() == ctm::ManagementState::PausedByUser &&
               state.ResumeRequested(false, false) && state.preparing(),
           "explicit recovery should unlock built-in preparation");
}

void TestOperationalFailureIsDistinctFromUserPause() {
    ctm::ManagementStateMachine state(ctm::TabProvider::BuiltIn);
    state.CompleteInitialization(false, false);
    state.PreparationCompleted(true);
    state.OperationFailed(true);
    Expect(state.state() == ctm::ManagementState::PausedByError &&
               state.can_resume(),
           "a safely restored runtime failure should be an error pause");
    state.OperationFailed(false);
    Expect(state.recovery_required(),
           "an incomplete runtime restoration should require recovery");
}

}  // namespace

int main() {
    TestBuiltInProviderPreparesWithoutWindowTabs();
    TestBuiltInProviderConflictIsSafeAndReversible();
    TestWindowTabsProviderWaitsAndAutomaticallyPrepares();
    TestUserPauseIsStickyAcrossProviderAndConflictChanges();
    TestResumeUsesTheSelectedProviderRules();
    TestFailuresCannotBypassRecovery();
    TestOperationalFailureIsDistinctFromUserPause();

    if (failures != 0) {
        std::cerr << failures << " management-state test(s) failed.\n";
        return 1;
    }
    std::cout << "All management-state tests passed.\n";
    return 0;
}
