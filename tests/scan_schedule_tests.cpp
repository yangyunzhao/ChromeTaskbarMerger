#include "scan_schedule.h"

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

[[nodiscard]] ctm::ScanSchedule::TimePoint AtMilliseconds(
    const std::int64_t milliseconds) {
    return ctm::ScanSchedule::TimePoint(std::chrono::milliseconds(milliseconds));
}

void TestFreshScheduleIsImmediatelyDue() {
    ctm::ScanSchedule schedule(std::chrono::seconds(2));
    Expect(schedule.IsDue(AtMilliseconds(1000)),
           "a fresh schedule should request its first scan immediately");
    Expect(schedule.DelayUntilDue(AtMilliseconds(1000)).count() == 0,
           "an immediately due scan should have zero delay");
}

void TestTwoSecondIntervalUsesAControllableClock() {
    ctm::ScanSchedule schedule(std::chrono::seconds(2));
    schedule.MarkScanned(AtMilliseconds(1000));
    Expect(!schedule.IsDue(AtMilliseconds(2999)),
           "the schedule should not fire before two seconds");
    Expect(schedule.DelayUntilDue(AtMilliseconds(2999)).count() == 1,
           "the remaining delay should be reported without a real wait");
    Expect(schedule.IsDue(AtMilliseconds(3000)),
           "the schedule should fire exactly at two seconds");
}

void TestImmediateRequestsAreCoalesced() {
    ctm::ScanSchedule schedule(std::chrono::seconds(2));
    schedule.MarkScanned(AtMilliseconds(1000));
    schedule.RequestImmediateScan();
    schedule.RequestImmediateScan();
    Expect(schedule.IsDue(AtMilliseconds(1001)),
           "multiple immediate requests should make one scan due");

    schedule.MarkScanned(AtMilliseconds(1001));
    Expect(!schedule.IsDue(AtMilliseconds(1002)),
           "marking the coalesced scan complete should clear the request");
    Expect(schedule.IsDue(AtMilliseconds(3001)),
           "the periodic interval should restart after the coalesced scan");
}

void TestManualScanDebouncesTheNextPeriodicScan() {
    ctm::ScanSchedule schedule(std::chrono::seconds(2));
    schedule.MarkScanned(AtMilliseconds(1000));
    schedule.RequestImmediateScan();
    schedule.MarkScanned(AtMilliseconds(1500));
    Expect(!schedule.IsDue(AtMilliseconds(3000)),
           "a manual scan should push back the next periodic scan");
    Expect(schedule.IsDue(AtMilliseconds(3500)),
           "the next periodic scan should be relative to the manual scan");
}

void TestInvalidIntervalIsClamped() {
    ctm::ScanSchedule schedule(std::chrono::milliseconds::zero());
    Expect(schedule.interval() == std::chrono::milliseconds(1),
           "a non-positive interval should be clamped to one millisecond");
}

}  // namespace

int main() {
    TestFreshScheduleIsImmediatelyDue();
    TestTwoSecondIntervalUsesAControllableClock();
    TestImmediateRequestsAreCoalesced();
    TestManualScanDebouncesTheNextPeriodicScan();
    TestInvalidIntervalIsClamped();
    if (failures != 0) {
        std::cerr << failures << " scan-schedule test(s) failed.\n";
        return 1;
    }
    std::cout << "All scan-schedule tests passed.\n";
    return 0;
}
