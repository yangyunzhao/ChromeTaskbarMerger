#include "tab_activation.h"
#include "tab_group_model.h"
#include "window_identity.h"

#include <Windows.h>

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] HWND TestHandle(const std::uintptr_t value) noexcept {
    return reinterpret_cast<HWND>(value);
}

[[nodiscard]] std::uintptr_t HandleValue(const HWND hwnd) noexcept {
    return reinterpret_cast<std::uintptr_t>(hwnd);
}

[[nodiscard]] ctm::WindowIdentity MakeIdentity(
    const std::uintptr_t handle,
    const DWORD process_id = 100,
    const DWORD thread_id = 200,
    const std::uint64_t creation_time = 300) {
    return {
        .hwnd = TestHandle(handle),
        .process_id = process_id,
        .thread_id = thread_id,
        .process_creation_time = creation_time,
        .class_name = L"Chrome_WidgetWin_1",
    };
}

[[nodiscard]] ctm::TabGroupCandidate MakeCandidate(
    const std::uintptr_t handle,
    const std::wstring_view title = L"Synthetic Chrome window",
    const DWORD process_id = 100,
    const DWORD thread_id = 200,
    const std::uint64_t creation_time = 300) {
    return {
        .identity =
            MakeIdentity(handle, process_id, thread_id, creation_time),
        .title = std::wstring(title),
    };
}

void ExpectOrder(const ctm::TabGroupModel& model,
                 const std::initializer_list<std::uintptr_t> handles,
                 const std::string_view description) {
    const std::span<const ctm::TabGroupMember> members = model.members();
    if (members.size() != handles.size()) {
        Expect(false, description);
        return;
    }
    std::size_t index = 0;
    for (const std::uintptr_t handle : handles) {
        if (HandleValue(members[index].identity.hwnd) != handle) {
            Expect(false, description);
            return;
        }
        ++index;
    }
}

void MakeActivatable(ctm::TabGroupModel* const model,
                     const ctm::WindowIdentity& identity) {
    Expect(model != nullptr && model->MarkTabCreated(identity, true),
           "the synthetic tab should be marked as created");
    Expect(model != nullptr &&
               model->MarkActivationPathVerified(identity, true),
           "the synthetic activation path should be verified");
}

void MakeTaskbarEligible(ctm::TabGroupModel* const model,
                         const ctm::WindowIdentity& identity) {
    MakeActivatable(model, identity);
    Expect(model != nullptr &&
               model->MarkRecoveryIntentPersisted(identity, true),
           "the synthetic recovery intent should be persisted");
}

class FakeWindowActivationGateway final
    : public ctm::IWindowActivationGateway {
public:
    [[nodiscard]] ctm::WindowActivationResult Activate(
        const ctm::WindowIdentity& identity) override {
        calls.push_back(identity);
        if (fail_next) {
            fail_next = false;
            return {
                .succeeded = false,
                .win32_error = ERROR_ACCESS_DENIED,
                .message = L"Configured activation failure.",
            };
        }
        return {
            .succeeded = true,
            .message = L"Configured activation success.",
        };
    }

    bool fail_next = false;
    std::vector<ctm::WindowIdentity> calls;
};

void TestEmptyGroupAndCandidateValidation() {
    ctm::TabGroupModel model;
    const ctm::TabGroupSyncReport empty = model.Synchronize({});
    Expect(model.members().empty(), "an empty snapshot should keep an empty group");
    Expect(!model.active_identity().has_value(),
           "an empty group should not have an active identity");
    Expect(empty.added_count == 0 && empty.removed_count == 0,
           "an empty synchronization should not report changes");

    ctm::TabGroupCandidate invalid = MakeCandidate(9);
    invalid.identity.process_creation_time = 0;
    const std::vector candidates = {
        MakeCandidate(1),
        MakeCandidate(1, L"Ambiguous reused handle", 101, 201, 301),
        invalid,
    };
    const ctm::TabGroupSyncReport report = model.Synchronize(candidates);
    Expect(report.added_count == 1 && report.ignored_candidate_count == 2,
           "incomplete and duplicate-HWND candidates should be ignored");
    ExpectOrder(model, {1}, "only the first valid handle should be retained");
}

void TestOneThreeAndFiveMemberGroups() {
    for (const std::size_t count : {std::size_t{1}, std::size_t{3},
                                    std::size_t{5}}) {
        ctm::TabGroupModel model;
        std::vector<ctm::TabGroupCandidate> candidates;
        for (std::size_t index = 0; index < count; ++index) {
            candidates.push_back(MakeCandidate(index + 1));
        }
        const ctm::TabGroupSyncReport report = model.Synchronize(candidates);
        Expect(model.members().size() == count,
               "the group size should match the valid candidate count");
        Expect(report.added_count == count,
               "each initial candidate should be reported as added");
        Expect(model.active_identity().has_value() &&
                   HandleValue(model.active_identity()->hwnd) == 1,
               "the first stable member should become active by default");
    }
}

void TestStableOrderAndTitleUpdates() {
    ctm::TabGroupModel model;
    const std::vector initial = {
        MakeCandidate(3, L"Three"),
        MakeCandidate(1, L"One"),
        MakeCandidate(2, L"Two"),
    };
    static_cast<void>(model.Synchronize(initial));

    const std::vector reordered = {
        MakeCandidate(2, L"Two"),
        MakeCandidate(3, L"Three updated"),
        MakeCandidate(1, L"One"),
        MakeCandidate(4, L"Four"),
    };
    const ctm::TabGroupSyncReport report = model.Synchronize(reordered);
    ExpectOrder(model, {3, 1, 2, 4},
                "existing order should remain stable and new members append");
    Expect(report.added_count == 1 && report.updated_title_count == 1,
           "synchronization should report the append and title update");
    Expect(model.members().front().title == L"Three updated",
           "a continuing member should receive its new title");
}

void TestRemovalAndPreferredActiveFallback() {
    ctm::TabGroupModel model;
    const std::vector initial = {
        MakeCandidate(1), MakeCandidate(2), MakeCandidate(3)};
    static_cast<void>(model.Synchronize(initial, initial[1].identity));
    Expect(model.active_identity().has_value() &&
               HandleValue(model.active_identity()->hwnd) == 2,
           "the preferred initial member should become active");

    const std::vector remaining = {MakeCandidate(1), MakeCandidate(3)};
    const ctm::TabGroupSyncReport report =
        model.Synchronize(remaining, remaining[1].identity);
    Expect(report.removed_count == 1 && report.active_changed,
           "removing the active member should report an active change");
    Expect(model.active_identity().has_value() &&
               HandleValue(model.active_identity()->hwnd) == 3,
           "the valid preferred fallback should become active");

    static_cast<void>(model.Synchronize({}));
    Expect(model.members().empty() && !model.active_identity().has_value(),
           "removing all candidates should clear the group and active member");
}

void TestReusedHandleResetsReachability() {
    ctm::TabGroupModel model;
    const ctm::TabGroupCandidate original =
        MakeCandidate(7, L"Original", 100, 200, 300);
    static_cast<void>(model.Synchronize(std::span(&original, 1)));
    model.SetTabStripHealthy(true);
    MakeTaskbarEligible(&model, original.identity);
    Expect(model.CanRemoveFromTaskbar(original.identity),
           "the original fully prepared identity should be taskbar eligible");

    const ctm::TabGroupCandidate replacement =
        MakeCandidate(7, L"Replacement", 101, 201, 301);
    const ctm::TabGroupSyncReport report =
        model.Synchronize(std::span(&replacement, 1));
    Expect(report.replaced_identity_count == 1,
           "a reused HWND with new process identity should be replaced");
    Expect(model.FindMember(original.identity) == nullptr,
           "the stale identity should no longer resolve");
    const ctm::TabGroupMember* const current =
        model.FindMember(replacement.identity);
    Expect(current != nullptr,
           "the replacement identity should remain in the same group slot");
    Expect(current != nullptr && !current->reachability.tab_created &&
               !current->reachability.activation_path_verified &&
               !current->reachability.recovery_intent_persisted,
           "identity replacement must reset all reachability evidence");
    Expect(!model.CanRemoveFromTaskbar(replacement.identity),
           "a replacement must not inherit taskbar-removal eligibility");
}

void TestReachabilityGateRequiresAllEvidence() {
    ctm::TabGroupModel model;
    const ctm::TabGroupCandidate candidate = MakeCandidate(10);
    static_cast<void>(model.Synchronize(std::span(&candidate, 1)));

    Expect(!model.MarkActivationPathVerified(candidate.identity, true),
           "activation cannot be verified before a tab exists");
    Expect(model.MarkRecoveryIntentPersisted(candidate.identity, true),
           "recovery intent may be persisted before tab creation");
    Expect(!model.CanRemoveFromTaskbar(candidate.identity),
           "recovery intent alone must not allow taskbar removal");
    Expect(model.MarkTabCreated(candidate.identity, true),
           "tab creation evidence should be accepted");
    Expect(!model.CanRemoveFromTaskbar(candidate.identity),
           "a tab without verified activation must not allow removal");
    Expect(model.MarkActivationPathVerified(candidate.identity, true),
           "activation evidence should be accepted after tab creation");
    Expect(!model.CanRemoveFromTaskbar(candidate.identity),
           "member evidence must not bypass an unhealthy tab strip");
    model.SetTabStripHealthy(true);
    Expect(model.CanRemoveFromTaskbar(candidate.identity),
           "the healthy strip and all member conditions should allow removal");

    model.SetTabStripHealthy(false);
    Expect(!model.CanRemoveFromTaskbar(candidate.identity),
           "tab-strip health loss should immediately block removal");
    model.SetTabStripHealthy(true);

    Expect(model.MarkTabCreated(candidate.identity, false),
           "tab loss should be recorded");
    Expect(!model.CanActivate(candidate.identity) &&
               !model.CanRemoveFromTaskbar(candidate.identity),
           "tab loss should also invalidate activation and removal eligibility");
}

void TestActivationGatewayIsInjectableAndFailureSafe() {
    ctm::TabGroupModel model;
    const std::vector candidates = {
        MakeCandidate(21), MakeCandidate(22), MakeCandidate(23)};
    static_cast<void>(model.Synchronize(candidates));
    MakeActivatable(&model, candidates[0].identity);
    MakeActivatable(&model, candidates[1].identity);

    FakeWindowActivationGateway gateway;
    ctm::TabActivationCoordinator activation(&model, &gateway);
    const ctm::TabActivationReport not_ready =
        activation.Activate(candidates[2].identity);
    Expect(!not_ready.succeeded && !not_ready.gateway_called &&
               gateway.calls.empty(),
           "an unready tab should be rejected before calling the gateway");

    const ctm::TabActivationReport success =
        activation.Activate(candidates[1].identity);
    Expect(success.succeeded && success.gateway_called &&
               gateway.calls.size() == 1,
           "a verified tab should use the injected activation gateway");
    Expect(model.active_identity().has_value() &&
               ctm::WindowIdentitiesMatch(
                   *model.active_identity(), candidates[1].identity),
           "successful activation should update the model's active identity");

    gateway.fail_next = true;
    const ctm::TabActivationReport failed =
        activation.Activate(candidates[0].identity);
    Expect(!failed.succeeded && failed.gateway_called &&
               failed.win32_error == ERROR_ACCESS_DENIED,
           "a configured gateway failure should be reported without hiding it");
    Expect(model.active_identity().has_value() &&
               ctm::WindowIdentitiesMatch(
                   *model.active_identity(), candidates[1].identity),
           "failed activation must not change the active member");
}

void TestWindowIdentityHelpers() {
    const ctm::WindowIdentity first = MakeIdentity(31, 101, 201, 301);
    const ctm::WindowIdentity same = MakeIdentity(31, 101, 201, 301);
    const ctm::WindowIdentity reused = MakeIdentity(31, 102, 202, 302);
    const ctm::WindowIdentity other_handle = MakeIdentity(32, 101, 201, 301);
    ctm::WindowIdentity incomplete = first;
    incomplete.class_name.clear();

    Expect(ctm::WindowIdentityIsComplete(first),
           "a populated synthetic identity should be complete");
    Expect(!ctm::WindowIdentityIsComplete(incomplete),
           "a missing class name should make an identity incomplete");
    Expect(ctm::WindowIdentitiesMatch(first, same),
           "equal complete identities should match");
    Expect(!ctm::WindowIdentitiesMatch(first, reused),
           "a reused handle with new process identity must not match");
    Expect(!ctm::WindowIdentitiesMatch(first, other_handle),
           "different HWNDs must not match even when values are equal");
}

}  // namespace

int main() {
    TestEmptyGroupAndCandidateValidation();
    TestOneThreeAndFiveMemberGroups();
    TestStableOrderAndTitleUpdates();
    TestRemovalAndPreferredActiveFallback();
    TestReusedHandleResetsReachability();
    TestReachabilityGateRequiresAllEvidence();
    TestActivationGatewayIsInjectableAndFailureSafe();
    TestWindowIdentityHelpers();

    if (failures != 0) {
        std::cerr << failures << " V2 tab-group model test(s) failed.\n";
        return 1;
    }
    std::cout << "All V2 tab-group model tests passed.\n";
    return 0;
}
