#include "tab_strip_layout.h"

#include <algorithm>
#include <limits>

namespace ctm {
namespace {

constexpr int kOuterPadding = 0;
constexpr int kTabGap = 1;
constexpr int kCloseButtonSize = 18;
constexpr int kCloseButtonMargin = 4;
constexpr int kMinimumWidthWithCloseButton = 34;

[[nodiscard]] int Scaled(const int value, const UINT dpi) noexcept {
    return MulDiv(
        value,
        static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi),
        USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] bool PointInside(const RECT& rectangle,
                               const POINT point) noexcept {
    return point.x >= rectangle.left && point.x < rectangle.right &&
           point.y >= rectangle.top && point.y < rectangle.bottom;
}

}  // namespace

TabStripLayout CalculateTabStripLayout(const SIZE client_size,
                                       const std::size_t tab_count,
                                       const UINT dpi,
                                       const int maximum_tab_width_pixels,
                                       const std::size_t first_visible_index,
                                       const TabStripAlignment content_alignment) {
    TabStripLayout layout;
    layout.client_size = client_size;
    const int outer_padding = Scaled(kOuterPadding, dpi);
    const int tab_gap = Scaled(kTabGap, dpi);
    const int maximum_tab_width = Scaled(
        std::clamp(
            maximum_tab_width_pixels,
            kMinimumTabWidthPixels,
            kMaximumTabWidthPixels),
        dpi);
    const int close_button_size = Scaled(kCloseButtonSize, dpi);
    const int close_button_margin = Scaled(kCloseButtonMargin, dpi);
    const int minimum_width_with_close_button =
        Scaled(kMinimumWidthWithCloseButton, dpi);
    const int minimum_tab_width = Scaled(kMinimumTabWidthPixels, dpi);
    if (client_size.cx <= outer_padding * 2 ||
        client_size.cy <= outer_padding * 2 || tab_count == 0 ||
        tab_count > static_cast<std::size_t>(
                        std::numeric_limits<int>::max())) {
        return layout;
    }

    const int count = static_cast<int>(tab_count);
    const int viewport_width = client_size.cx - outer_padding * 2;
    if (viewport_width < minimum_tab_width) {
        return layout;
    }
    layout.viewport_bounds = {
        .left = outer_padding,
        .top = outer_padding,
        .right = client_size.cx - outer_padding,
        .bottom = client_size.cy,
    };

    const int total_gap = tab_gap * (count - 1);
    const int available_for_all = viewport_width - total_gap;
    const bool overflowed = available_for_all < minimum_tab_width * count;
    int tab_width = 0;
    int remainder = 0;
    std::size_t safe_first = 0;
    if (overflowed) {
        const int capacity = std::max(
            1, (viewport_width + tab_gap) / (minimum_tab_width + tab_gap));
        layout.visible_capacity = std::min(
            tab_count, static_cast<std::size_t>(capacity));
        const int visible_gaps =
            tab_gap * (static_cast<int>(layout.visible_capacity) - 1);
        tab_width = std::min(
            maximum_tab_width,
            (viewport_width - visible_gaps) /
                static_cast<int>(layout.visible_capacity));
        safe_first = std::min(
            first_visible_index, tab_count - layout.visible_capacity);
        layout.overflowed = true;
        layout.first_visible_index = safe_first;
    } else {
        const int natural_width = available_for_all / count;
        tab_width = std::min(natural_width, maximum_tab_width);
        remainder = natural_width < maximum_tab_width
                        ? available_for_all % count
                        : 0;
        layout.visible_capacity = tab_count;
    }

    int content_offset = 0;
    if (!overflowed) {
        const int used_width = tab_width * count + total_gap + remainder;
        const int unused_width = std::max(viewport_width - used_width, 0);
        if (content_alignment == TabStripAlignment::Center) {
            content_offset = unused_width / 2;
        } else if (content_alignment == TabStripAlignment::Right) {
            content_offset = unused_width;
        }
    }
    int left = outer_padding + content_offset -
               static_cast<int>(safe_first) * (tab_width + tab_gap);
    layout.items.reserve(tab_count);
    for (int index = 0; index < count; ++index) {
        const int extra = remainder > 0 ? 1 : 0;
        remainder -= extra;
        const int width = tab_width + extra;
        TabLayoutItem item;
        item.bounds = {
            .left = left,
            .top = outer_padding,
            .right = left + width,
            .bottom = client_size.cy,
        };
        if (width >= minimum_width_with_close_button) {
            const int close_top = item.bounds.top +
                                  ((item.bounds.bottom - item.bounds.top) -
                                   close_button_size) /
                                      2;
            item.close_bounds = {
                .left = item.bounds.right - close_button_margin -
                        close_button_size,
                .top = close_top,
                .right = item.bounds.right - close_button_margin,
                .bottom = close_top + close_button_size,
            };
        }
        layout.items.push_back(item);
        left = item.bounds.right + tab_gap;
    }
    return layout;
}

RECT CalculateConfiguredTabStripBounds(
    const RECT& group_bounds,
    const int tab_strip_height,
    const TabStripAlignment alignment,
    const int width_percent) noexcept {
    const int group_width = group_bounds.right - group_bounds.left;
    const int group_height = group_bounds.bottom - group_bounds.top;
    if (group_width <= 0 || group_height <= 0 ||
        tab_strip_height <= 0 || tab_strip_height >= group_height) {
        return {};
    }
    const int safe_percent = std::clamp(
        width_percent,
        kMinimumTabStripWidthPercent,
        kMaximumTabStripWidthPercent);
    const int strip_width = std::max(
        1, MulDiv(group_width, safe_percent, 100));
    int left = group_bounds.left;
    if (alignment == TabStripAlignment::Center) {
        left += (group_width - strip_width) / 2;
    } else if (alignment == TabStripAlignment::Right) {
        left = group_bounds.right - strip_width;
    }
    return {
        .left = left,
        .top = group_bounds.top,
        .right = left + strip_width,
        .bottom = group_bounds.top + tab_strip_height,
    };
}

RECT CalculateV31TabStripBounds(
    const RECT& chrome_bounds,
    const int tab_strip_height,
    const TabStripSurfaceMode surface_mode,
    const TabStripAlignment alignment,
    const int width_percent,
    const int caption_control_reserve_width) noexcept {
    const int chrome_width = chrome_bounds.right - chrome_bounds.left;
    const int chrome_height = chrome_bounds.bottom - chrome_bounds.top;
    if (chrome_width <= 0 || chrome_height <= 0 || tab_strip_height <= 0 ||
        (surface_mode == TabStripSurfaceMode::MaximizedOverlay &&
         tab_strip_height >= chrome_height)) {
        return {};
    }

    const int reserve = surface_mode == TabStripSurfaceMode::MaximizedOverlay
                            ? std::clamp(
                                  caption_control_reserve_width,
                                  0,
                                  chrome_width - 1)
                            : 0;
    const int available_width = chrome_width - reserve;
    const int safe_percent = std::clamp(
        width_percent,
        kMinimumTabStripWidthPercent,
        kMaximumTabStripWidthPercent);
    const int requested_width = std::max(
        1, MulDiv(chrome_width, safe_percent, 100));
    const int strip_width = std::min(requested_width, available_width);

    int left = chrome_bounds.left;
    if (alignment == TabStripAlignment::Center) {
        left += (available_width - strip_width) / 2;
    } else if (alignment == TabStripAlignment::Right) {
        left += available_width - strip_width;
    }
    const int top =
        surface_mode == TabStripSurfaceMode::AttachedAbove
            ? chrome_bounds.top - tab_strip_height
            : chrome_bounds.top;
    return {
        .left = left,
        .top = top,
        .right = left + strip_width,
        .bottom = top + tab_strip_height,
    };
}

int CalculateCaptionControlReserveWidth(
    const int caption_button_width,
    const int caption_button_height,
    const int frame_width,
    const int padded_border_width) noexcept {
    const int safe_width = std::max(caption_button_width, 0);
    const int safe_height = std::max(caption_button_height, 0);
    const int safe_frame = std::max(frame_width, 0);
    const int safe_border = std::max(padded_border_width, 0);
    // Chromium uses a custom title bar whose buttons can be wider than the
    // classic SM_CXSIZE metric. Two caption-button height units are a
    // conservative DPI-aware lower bound for each of the three controls.
    const int control_width = std::max(safe_width, safe_height * 2);
    return control_width * 3 + (safe_frame + safe_border) * 2;
}

RECT CalculateCompactTabStripBounds(
    const RECT& available_bounds,
    const std::size_t tab_count,
    const UINT dpi,
    const int maximum_tab_width_pixels,
    const TabStripAlignment alignment) noexcept {
    const int available_width =
        available_bounds.right - available_bounds.left;
    const int available_height =
        available_bounds.bottom - available_bounds.top;
    if (available_width <= 0 || available_height <= 0 || tab_count == 0 ||
        tab_count > static_cast<std::size_t>(
                        std::numeric_limits<int>::max())) {
        return {};
    }
    const int count = static_cast<int>(tab_count);
    const int maximum_tab_width = Scaled(
        std::clamp(
            maximum_tab_width_pixels,
            kMinimumTabWidthPixels,
            kMaximumTabWidthPixels),
        dpi);
    const int tab_gap = Scaled(kTabGap, dpi);
    const long long preferred =
        static_cast<long long>(maximum_tab_width) * count +
        static_cast<long long>(tab_gap) * (count - 1);
    const int compact_width = static_cast<int>(std::min<long long>(
        available_width, preferred));
    int left = available_bounds.left;
    if (alignment == TabStripAlignment::Center) {
        left += (available_width - compact_width) / 2;
    } else if (alignment == TabStripAlignment::Right) {
        left = available_bounds.right - compact_width;
    }
    return {
        .left = left,
        .top = available_bounds.top,
        .right = left + compact_width,
        .bottom = available_bounds.bottom,
    };
}

RECT AdjustTabStripBoundsForInvisibleFrame(
    const RECT& available_bounds,
    const RECT& owner_window_bounds,
    const RECT& owner_visible_bounds) noexcept {
    const int available_width =
        available_bounds.right - available_bounds.left;
    const int owner_width =
        owner_window_bounds.right - owner_window_bounds.left;
    const int visible_width =
        owner_visible_bounds.right - owner_visible_bounds.left;
    if (available_width <= 0 || owner_width <= 0 || visible_width <= 0) {
        return available_bounds;
    }

    const int visible_left = std::clamp(
        static_cast<int>(owner_visible_bounds.left),
        static_cast<int>(owner_window_bounds.left),
        static_cast<int>(owner_window_bounds.right));
    const int visible_right = std::clamp(
        static_cast<int>(owner_visible_bounds.right),
        static_cast<int>(owner_window_bounds.left),
        static_cast<int>(owner_window_bounds.right));
    RECT adjusted = available_bounds;
    adjusted.left = std::max(
        static_cast<int>(adjusted.left), visible_left);
    adjusted.right = std::min(
        static_cast<int>(adjusted.right), visible_right);
    return adjusted.right > adjusted.left ? adjusted : available_bounds;
}

TabHitResult HitTestTabStrip(const TabStripLayout& layout,
                            const POINT point) noexcept {
    if (!PointInside(layout.viewport_bounds, point)) {
        return {};
    }
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        const TabLayoutItem& item = layout.items[index];
        if (!PointInside(item.bounds, point)) {
            continue;
        }
        if (item.close_bounds.right > item.close_bounds.left &&
            PointInside(item.close_bounds, point)) {
            return {
                .region = TabHitRegion::Close,
                .index = index,
            };
        }
        return {
            .region = TabHitRegion::Body,
            .index = index,
        };
    }
    return {};
}

std::size_t CalculateRelativeTabIndex(
    const std::size_t current_index,
    const std::size_t tab_count,
    const int direction) noexcept {
    if (tab_count == 0 || direction == 0) {
        return tab_count == 0 ? 0 : std::min(current_index, tab_count - 1U);
    }
    const std::size_t current = std::min(current_index, tab_count - 1U);
    if (direction > 0) {
        return current + 1U < tab_count ? current + 1U : 0;
    }
    return current == 0 ? tab_count - 1U : current - 1U;
}

}  // namespace ctm
