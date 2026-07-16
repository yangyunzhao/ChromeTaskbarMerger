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
                                       const UINT dpi) {
    TabStripLayout layout;
    layout.client_size = client_size;
    const int outer_padding = Scaled(kOuterPadding, dpi);
    const int tab_gap = Scaled(kTabGap, dpi);
    const int maximum_tab_width = Scaled(kMaximumTabWidth, dpi);
    const int close_button_size = Scaled(kCloseButtonSize, dpi);
    const int close_button_margin = Scaled(kCloseButtonMargin, dpi);
    const int minimum_width_with_close_button =
        Scaled(kMinimumWidthWithCloseButton, dpi);
    if (client_size.cx <= outer_padding * 2 ||
        client_size.cy <= outer_padding * 2 || tab_count == 0 ||
        tab_count > static_cast<std::size_t>(
                        std::numeric_limits<int>::max())) {
        return layout;
    }

    const int count = static_cast<int>(tab_count);
    const int total_gap = tab_gap * (count - 1);
    const int available = client_size.cx - outer_padding * 2 - total_gap;
    if (available < count) {
        return layout;
    }

    const int natural_width = available / count;
    const int tab_width = std::min(natural_width, maximum_tab_width);
    int remainder =
        natural_width < maximum_tab_width ? available % count : 0;
    int left = outer_padding;
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
            .bottom = client_size.cy - outer_padding,
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
