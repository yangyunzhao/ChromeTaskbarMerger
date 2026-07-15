#pragma once

#include <chrono>

namespace ctm {

class ScanSchedule final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ScanSchedule(std::chrono::milliseconds interval) noexcept;

    void MarkScanned(TimePoint now) noexcept;
    void RequestImmediateScan() noexcept;

    [[nodiscard]] bool IsDue(TimePoint now) const noexcept;
    [[nodiscard]] std::chrono::milliseconds DelayUntilDue(
        TimePoint now) const noexcept;
    [[nodiscard]] std::chrono::milliseconds interval() const noexcept {
        return interval_;
    }

private:
    std::chrono::milliseconds interval_;
    TimePoint next_scan_{};
    bool initialized_ = false;
    bool immediate_scan_requested_ = false;
};

}  // namespace ctm
