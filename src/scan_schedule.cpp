#include "scan_schedule.h"

namespace ctm {

ScanSchedule::ScanSchedule(
    const std::chrono::milliseconds interval) noexcept
    : interval_(interval.count() > 0 ? interval
                                    : std::chrono::milliseconds(1)) {}

void ScanSchedule::MarkScanned(const TimePoint now) noexcept {
    initialized_ = true;
    immediate_scan_requested_ = false;
    next_scan_ = now + interval_;
}

void ScanSchedule::RequestImmediateScan() noexcept {
    immediate_scan_requested_ = true;
}

bool ScanSchedule::IsDue(const TimePoint now) const noexcept {
    return immediate_scan_requested_ || !initialized_ || now >= next_scan_;
}

std::chrono::milliseconds ScanSchedule::DelayUntilDue(
    const TimePoint now) const noexcept {
    if (IsDue(now)) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::ceil<std::chrono::milliseconds>(next_scan_ - now);
}

}  // namespace ctm
