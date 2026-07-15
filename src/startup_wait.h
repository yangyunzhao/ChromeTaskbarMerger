#pragma once

#include <chrono>

namespace ctm {

inline constexpr std::chrono::milliseconds kStartupWaitRetryInterval{3000};
inline constexpr std::chrono::milliseconds kStartupWaitTimeout{120000};

class StartupWaitSchedule final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    StartupWaitSchedule(
        std::chrono::milliseconds retry_interval = kStartupWaitRetryInterval,
        std::chrono::milliseconds timeout = kStartupWaitTimeout) noexcept;

    void Start(TimePoint now) noexcept;
    void Stop() noexcept;
    void MarkAttempted(TimePoint now) noexcept;

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] bool IsAttemptDue(TimePoint now) const noexcept;
    [[nodiscard]] bool HasTimedOut(TimePoint now) const noexcept;
    [[nodiscard]] std::chrono::milliseconds retry_interval() const noexcept {
        return retry_interval_;
    }
    [[nodiscard]] std::chrono::milliseconds timeout() const noexcept {
        return timeout_;
    }

private:
    std::chrono::milliseconds retry_interval_;
    std::chrono::milliseconds timeout_;
    TimePoint deadline_{};
    TimePoint next_attempt_{};
    bool active_ = false;
};

}  // namespace ctm
