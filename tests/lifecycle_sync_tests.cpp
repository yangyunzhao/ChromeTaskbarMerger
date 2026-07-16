#include "lifecycle_sync.h"
#include "win_event_monitor.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
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

[[nodiscard]] ctm::LifecycleSyncSchedule::TimePoint AtMilliseconds(
    const std::int64_t milliseconds) {
    return ctm::LifecycleSyncSchedule::TimePoint(
        std::chrono::milliseconds(milliseconds));
}

[[nodiscard]] HWND TestHandle(const std::uintptr_t value) noexcept {
    return reinterpret_cast<HWND>(value);
}

void TestInitialEventDebounceStormBoundAndFallback() {
    ctm::LifecycleSyncSchedule schedule(
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(500),
        std::chrono::seconds(2));
    Expect(schedule.DueReason(AtMilliseconds(1000)) ==
               ctm::LifecycleSyncReason::Initial,
           "a fresh lifecycle schedule should be due immediately");
    schedule.MarkSynchronized(AtMilliseconds(1000));
    Expect(schedule.DueReason(AtMilliseconds(2999)) ==
               ctm::LifecycleSyncReason::None &&
               schedule.DelayUntilDue(AtMilliseconds(2999)).count() == 1,
           "the fallback scan should use the controllable two-second clock");

    for (std::int64_t time = 1100; time <= 1500; time += 50) {
        schedule.RecordEvent(
            {.kind = ctm::ChromeWindowEventKind::NameChanged,
             .hwnd = TestHandle(1)},
            AtMilliseconds(time));
    }
    Expect(schedule.pending_event_count() == 9,
           "the storm should retain its diagnostic event count");
    Expect(schedule.DueReason(AtMilliseconds(1599)) ==
               ctm::LifecycleSyncReason::None,
           "the last event should still receive its debounce interval");
    Expect(schedule.DueReason(AtMilliseconds(1600)) ==
               ctm::LifecycleSyncReason::EventBatch,
           "a continuous storm should be bounded by maximum event delay");
    schedule.MarkSynchronized(AtMilliseconds(1600));
    Expect(schedule.pending_event_count() == 0 &&
               schedule.DueReason(AtMilliseconds(3599)) ==
                   ctm::LifecycleSyncReason::None &&
               schedule.DueReason(AtMilliseconds(3600)) ==
                   ctm::LifecycleSyncReason::FallbackScan,
           "one coalesced synchronization should reset the fallback deadline");
}

void TestDestroyHintsAreUniqueAndClearedAfterSynchronization() {
    ctm::LifecycleSyncSchedule schedule(
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(500),
        std::chrono::seconds(2));
    schedule.MarkSynchronized(AtMilliseconds(0));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Destroyed,
         .hwnd = TestHandle(10)},
        AtMilliseconds(10));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Destroyed,
         .hwnd = TestHandle(10)},
        AtMilliseconds(20));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Destroyed,
         .hwnd = TestHandle(11)},
        AtMilliseconds(30));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Shown,
         .hwnd = TestHandle(12)},
        AtMilliseconds(40));
    Expect(schedule.destroyed_handles().size() == 2,
           "duplicate destroy notifications should coalesce by HWND");
    schedule.MarkSynchronized(AtMilliseconds(140));
    Expect(schedule.destroyed_handles().empty(),
           "successful synchronization should clear destroy hints");
}

void TestTransientDeferralPreservesLifecycleEvidence() {
    ctm::LifecycleSyncSchedule schedule(
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(500),
        std::chrono::seconds(2));
    schedule.MarkSynchronized(AtMilliseconds(0));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Destroyed,
         .hwnd = TestHandle(20)},
        AtMilliseconds(100));
    schedule.RecordEvent(
        {.kind = ctm::ChromeWindowEventKind::Foreground,
         .hwnd = TestHandle(21)},
        AtMilliseconds(110));

    schedule.DeferSynchronizationUntil(AtMilliseconds(400));
    Expect(schedule.DueReason(AtMilliseconds(399)) ==
               ctm::LifecycleSyncReason::None &&
               schedule.DelayUntilDue(AtMilliseconds(399)).count() == 1,
           "a transiently unavailable window should postpone the batch");
    Expect(schedule.destroyed_handles().size() == 1 &&
               schedule.destroyed_handles().front() == TestHandle(20) &&
               schedule.foreground_hint() == TestHandle(21) &&
               schedule.pending_event_count() == 2,
           "deferral should preserve destroy, foreground, and event evidence");
    Expect(schedule.DueReason(AtMilliseconds(400)) ==
               ctm::LifecycleSyncReason::EventBatch,
           "the preserved event batch should retry at the requested time");

    schedule.MarkSynchronized(AtMilliseconds(400));
    Expect(schedule.destroyed_handles().empty() &&
               schedule.foreground_hint() == nullptr &&
               schedule.pending_event_count() == 0 &&
               schedule.DueReason(AtMilliseconds(2399)) ==
                   ctm::LifecycleSyncReason::None,
           "a successful retry should clear deferred lifecycle evidence");
}

void TestNativeEventTranslationFiltersChildNoise() {
    Expect(ctm::TranslateWinEvent(
               EVENT_OBJECT_CREATE, OBJID_WINDOW, CHILDID_SELF) ==
               ctm::ChromeWindowEventKind::Created,
           "top-level create should be translated");
    Expect(ctm::TranslateWinEvent(
               EVENT_OBJECT_DESTROY, OBJID_WINDOW, CHILDID_SELF) ==
               ctm::ChromeWindowEventKind::Destroyed,
           "top-level destroy should be translated");
    Expect(ctm::TranslateWinEvent(
               EVENT_OBJECT_NAMECHANGE, OBJID_WINDOW, CHILDID_SELF) ==
               ctm::ChromeWindowEventKind::NameChanged,
           "window-name change should be translated");
    Expect(!ctm::TranslateWinEvent(
                EVENT_OBJECT_NAMECHANGE, OBJID_CLIENT, CHILDID_SELF)
                .has_value(),
           "client-object noise should not reach the lifecycle queue");
    Expect(ctm::TranslateWinEvent(
               EVENT_SYSTEM_FOREGROUND, OBJID_CLIENT, 7) ==
               ctm::ChromeWindowEventKind::Foreground,
           "foreground events should not depend on object identifiers");
    Expect(ctm::TranslateWinEvent(
               EVENT_SYSTEM_MINIMIZESTART, OBJID_WINDOW, CHILDID_SELF) ==
               ctm::ChromeWindowEventKind::MinimizeStarted,
           "minimize-start should be translated");
    Expect(ctm::TranslateWinEvent(
               EVENT_SYSTEM_MINIMIZEEND, OBJID_WINDOW, CHILDID_SELF) ==
               ctm::ChromeWindowEventKind::MinimizeEnded,
           "minimize-end should be translated");
}

void TestMessageEncodingIsLossless() {
    const HWND hwnd = TestHandle(0x1234);
    const ctm::ChromeWindowEvent decoded =
        ctm::ChromeWinEventMonitor::DecodeMessage(
            reinterpret_cast<WPARAM>(hwnd),
            static_cast<LPARAM>(ctm::ChromeWindowEventKind::Hidden));
    Expect(decoded.kind == ctm::ChromeWindowEventKind::Hidden &&
               decoded.hwnd == hwnd,
           "the lightweight callback message should preserve event and HWND");
}

void TestOutOfContextMonitorPostsOnlyToTheOwnerMessageQueue() {
    constexpr UINT message_id = WM_APP + 0x71;
    MSG message{};
    static_cast<void>(PeekMessageW(
        &message, nullptr, WM_USER, WM_USER, PM_NOREMOVE));
    ctm::ChromeWinEventMonitor monitor;
    DWORD error = ERROR_SUCCESS;
    Expect(monitor.Start(
               GetCurrentThreadId(), message_id, false, &error) &&
               monitor.active() && error == ERROR_SUCCESS,
           "an out-of-context WinEvent hook should install for the owner thread");
    if (!monitor.active()) {
        return;
    }

    const HWND expected = TestHandle(0x4567);
    ctm::ChromeWinEventMonitor::WinEventCallback(
        nullptr,
        EVENT_OBJECT_NAMECHANGE,
        expected,
        OBJID_WINDOW,
        CHILDID_SELF,
        GetCurrentThreadId(),
        0);
    bool received_expected = false;
    while (PeekMessageW(
               &message, nullptr, message_id, message_id, PM_REMOVE) !=
           FALSE) {
        const ctm::ChromeWindowEvent decoded =
            ctm::ChromeWinEventMonitor::DecodeMessage(
                message.wParam, message.lParam);
        if (decoded.hwnd == expected &&
            decoded.kind == ctm::ChromeWindowEventKind::NameChanged) {
            received_expected = true;
        }
    }
    Expect(received_expected,
           "the callback should post one lightweight thread message");
    monitor.Stop();
    Expect(!monitor.active(),
           "stopping the event monitor should release its native hook");
}

}  // namespace

int main() {
    TestInitialEventDebounceStormBoundAndFallback();
    TestDestroyHintsAreUniqueAndClearedAfterSynchronization();
    TestTransientDeferralPreservesLifecycleEvidence();
    TestNativeEventTranslationFiltersChildNoise();
    TestMessageEncodingIsLossless();
    TestOutOfContextMonitorPostsOnlyToTheOwnerMessageQueue();

    if (failures != 0) {
        std::cerr << failures << " lifecycle-sync test(s) failed.\n";
        return 1;
    }
    std::cout << "All lifecycle-sync tests passed.\n";
    return 0;
}
