#include "startup_wait.h"

namespace ctm {

StartupWaitSchedule::StartupWaitSchedule(
    const std::chrono::milliseconds retry_interval,
    const std::chrono::milliseconds timeout) noexcept
    : retry_interval_(retry_interval.count() > 0
                          ? retry_interval
                          : std::chrono::milliseconds(1)),
      timeout_(timeout.count() > 0 ? timeout : std::chrono::milliseconds(1)) {}

void StartupWaitSchedule::Start(const TimePoint now) noexcept {
    active_ = true;
    next_attempt_ = now + retry_interval_;
    deadline_ = now + timeout_;
}

void StartupWaitSchedule::Stop() noexcept {
    active_ = false;
}

void StartupWaitSchedule::MarkAttempted(const TimePoint now) noexcept {
    if (active_) {
        next_attempt_ = now + retry_interval_;
    }
}

bool StartupWaitSchedule::IsAttemptDue(const TimePoint now) const noexcept {
    return active_ && now >= next_attempt_ && now < deadline_;
}

bool StartupWaitSchedule::HasTimedOut(const TimePoint now) const noexcept {
    return active_ && now >= deadline_;
}

}  // namespace ctm
