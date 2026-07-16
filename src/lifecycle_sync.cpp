#include "lifecycle_sync.h"

#include <algorithm>

namespace ctm {
namespace {

[[nodiscard]] std::chrono::milliseconds PositiveOrOne(
    const std::chrono::milliseconds value) noexcept {
    return value.count() > 0 ? value : std::chrono::milliseconds(1);
}

}  // namespace

LifecycleSyncSchedule::LifecycleSyncSchedule(
    const std::chrono::milliseconds debounce_interval,
    const std::chrono::milliseconds maximum_event_delay,
    const std::chrono::milliseconds fallback_interval) noexcept
    : debounce_interval_(PositiveOrOne(debounce_interval)),
      maximum_event_delay_(PositiveOrOne(maximum_event_delay)),
      fallback_interval_(PositiveOrOne(fallback_interval)) {}

void LifecycleSyncSchedule::RecordEvent(
    const ChromeWindowEvent& event,
    const TimePoint now) {
    if (pending_event_count_ == 0) {
        first_event_ = now;
    }
    last_event_ = now;
    ++pending_event_count_;
    if (event.kind == ChromeWindowEventKind::Destroyed &&
        event.hwnd != nullptr &&
        std::find(
            destroyed_handles_.begin(),
            destroyed_handles_.end(),
            event.hwnd) == destroyed_handles_.end()) {
        destroyed_handles_.push_back(event.hwnd);
    }
    if (event.kind == ChromeWindowEventKind::Foreground &&
        event.hwnd != nullptr) {
        foreground_hint_ = event.hwnd;
    }
}

void LifecycleSyncSchedule::DeferSynchronizationUntil(
    const TimePoint retry_at) noexcept {
    initialized_ = true;
    deferred_until_ = retry_at;
}

void LifecycleSyncSchedule::MarkSynchronized(const TimePoint now) noexcept {
    initialized_ = true;
    next_fallback_ = now + fallback_interval_;
    pending_event_count_ = 0;
    deferred_until_.reset();
    destroyed_handles_.clear();
    foreground_hint_ = nullptr;
}

LifecycleSyncReason LifecycleSyncSchedule::DueReason(
    const TimePoint now) const noexcept {
    if (!initialized_) {
        return LifecycleSyncReason::Initial;
    }
    if (deferred_until_.has_value()) {
        if (now < *deferred_until_) {
            return LifecycleSyncReason::None;
        }
        return pending_event_count_ != 0
                   ? LifecycleSyncReason::EventBatch
                   : LifecycleSyncReason::FallbackScan;
    }
    if (pending_event_count_ != 0) {
        const TimePoint debounce_due = last_event_ + debounce_interval_;
        const TimePoint bounded_due = first_event_ + maximum_event_delay_;
        if (now >= std::min(debounce_due, bounded_due)) {
            return LifecycleSyncReason::EventBatch;
        }
    }
    if (now >= next_fallback_) {
        return LifecycleSyncReason::FallbackScan;
    }
    return LifecycleSyncReason::None;
}

std::chrono::milliseconds LifecycleSyncSchedule::DelayUntilDue(
    const TimePoint now) const noexcept {
    if (DueReason(now) != LifecycleSyncReason::None) {
        return std::chrono::milliseconds::zero();
    }
    if (deferred_until_.has_value()) {
        return std::chrono::ceil<std::chrono::milliseconds>(
            *deferred_until_ - now);
    }
    TimePoint due = next_fallback_;
    if (pending_event_count_ != 0) {
        due = std::min(
            due,
            std::min(
                last_event_ + debounce_interval_,
                first_event_ + maximum_event_delay_));
    }
    return std::chrono::ceil<std::chrono::milliseconds>(due - now);
}

}  // namespace ctm
