#include "tab_strip_layout.h"

#include <algorithm>
#include <limits>

namespace ctm {
namespace {

constexpr int kOuterPadding = 4;
constexpr int kTabGap = 2;
constexpr int kMaximumTabWidth = 240;
constexpr int kCloseButtonSize = 18;
constexpr int kCloseButtonMargin = 4;
constexpr int kMinimumWidthWithCloseButton = 34;

[[nodiscard]] bool PointInside(const RECT& rectangle,
                               const POINT point) noexcept {
    return point.x >= rectangle.left && point.x < rectangle.right &&
           point.y >= rectangle.top && point.y < rectangle.bottom;
}

}  // namespace

TabStripLayout CalculateTabStripLayout(const SIZE client_size,
                                       const std::size_t tab_count) {
    TabStripLayout layout;
    layout.client_size = client_size;
    if (client_size.cx <= kOuterPadding * 2 ||
        client_size.cy <= kOuterPadding * 2 || tab_count == 0 ||
        tab_count > static_cast<std::size_t>(
                        std::numeric_limits<int>::max())) {
        return layout;
    }

    const int count = static_cast<int>(tab_count);
    const int total_gap = kTabGap * (count - 1);
    const int available = client_size.cx - kOuterPadding * 2 - total_gap;
    if (available < count) {
        return layout;
    }

    const int natural_width = available / count;
    const int tab_width = std::min(natural_width, kMaximumTabWidth);
    int remainder = natural_width < kMaximumTabWidth ? available % count : 0;
    int left = kOuterPadding;
    layout.items.reserve(tab_count);
    for (int index = 0; index < count; ++index) {
        const int extra = remainder > 0 ? 1 : 0;
        remainder -= extra;
        const int width = tab_width + extra;
        TabLayoutItem item;
        item.bounds = {
            .left = left,
            .top = kOuterPadding,
            .right = left + width,
            .bottom = client_size.cy - kOuterPadding,
        };
        if (width >= kMinimumWidthWithCloseButton) {
            const int close_top = item.bounds.top +
                                  ((item.bounds.bottom - item.bounds.top) -
                                   kCloseButtonSize) /
                                      2;
            item.close_bounds = {
                .left = item.bounds.right - kCloseButtonMargin -
                        kCloseButtonSize,
                .top = close_top,
                .right = item.bounds.right - kCloseButtonMargin,
                .bottom = close_top + kCloseButtonSize,
            };
        }
        layout.items.push_back(item);
        left = item.bounds.right + kTabGap;
    }
    return layout;
}

TabHitResult HitTestTabStrip(const TabStripLayout& layout,
                            const POINT point) noexcept {
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

}  // namespace ctm
