#pragma once

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace ctm {

enum class ChromeWindowEventKind : LPARAM {
    Created = 1,
    Destroyed,
    Shown,
    Hidden,
    NameChanged,
    Foreground,
    MinimizeStarted,
    MinimizeEnded,
    LocationChanged,
};

struct ChromeWindowEvent {
    ChromeWindowEventKind kind = ChromeWindowEventKind::Created;
    HWND hwnd = nullptr;
};

enum class LifecycleSyncReason {
    None,
    Initial,
    EventBatch,
    FallbackScan,
};

class LifecycleSyncSchedule final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    LifecycleSyncSchedule(
        std::chrono::milliseconds debounce_interval,
        std::chrono::milliseconds maximum_event_delay,
        std::chrono::milliseconds fallback_interval) noexcept;

    void RecordEvent(const ChromeWindowEvent& event, TimePoint now);
    void DeferSynchronizationUntil(TimePoint retry_at) noexcept;
    void MarkSynchronized(TimePoint now) noexcept;

    [[nodiscard]] LifecycleSyncReason DueReason(TimePoint now) const noexcept;
    [[nodiscard]] std::chrono::milliseconds DelayUntilDue(
        TimePoint now) const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept {
        return pending_event_count_;
    }
    [[nodiscard]] std::span<const HWND> destroyed_handles() const noexcept {
        return destroyed_handles_;
    }
    [[nodiscard]] HWND foreground_hint() const noexcept {
        return foreground_hint_;
    }

private:
    std::chrono::milliseconds debounce_interval_;
    std::chrono::milliseconds maximum_event_delay_;
    std::chrono::milliseconds fallback_interval_;
    TimePoint first_event_{};
    TimePoint last_event_{};
    TimePoint next_fallback_{};
    std::size_t pending_event_count_ = 0;
    bool initialized_ = false;
    std::optional<TimePoint> deferred_until_;
    std::vector<HWND> destroyed_handles_;
    HWND foreground_hint_ = nullptr;
};

class GeometrySyncSchedule final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    GeometrySyncSchedule(
        std::chrono::milliseconds debounce_interval,
        std::chrono::milliseconds maximum_event_delay) noexcept;

    void RecordEvent(const ChromeWindowEvent& event, TimePoint now) noexcept;
    void MarkSynchronized() noexcept;

    [[nodiscard]] bool IsDue(TimePoint now) const noexcept;
    [[nodiscard]] std::chrono::milliseconds DelayUntilDue(
        TimePoint now) const noexcept;
    [[nodiscard]] bool pending() const noexcept {
        return pending_event_count_ != 0;
    }
    [[nodiscard]] std::size_t pending_event_count() const noexcept {
        return pending_event_count_;
    }
    [[nodiscard]] HWND location_hint() const noexcept {
        return location_hint_;
    }
    [[nodiscard]] HWND minimize_started_hint() const noexcept {
        return minimize_started_hint_;
    }
    [[nodiscard]] HWND minimize_ended_hint() const noexcept {
        return minimize_ended_hint_;
    }

private:
    std::chrono::milliseconds debounce_interval_;
    std::chrono::milliseconds maximum_event_delay_;
    TimePoint first_event_{};
    TimePoint last_event_{};
    std::size_t pending_event_count_ = 0;
    HWND location_hint_ = nullptr;
    HWND minimize_started_hint_ = nullptr;
    HWND minimize_ended_hint_ = nullptr;
};

}  // namespace ctm
