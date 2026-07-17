#pragma once

#include "app_config.h"

#include <Windows.h>

#include <cstddef>
#include <vector>

namespace ctm {

inline constexpr int kV2TabStripHeight = 38;

enum class TabHitRegion {
    None,
    Body,
    Close,
};

struct TabLayoutItem {
    RECT bounds{};
    RECT close_bounds{};
};

struct TabStripLayout {
    SIZE client_size{};
    RECT viewport_bounds{};
    std::vector<TabLayoutItem> items;
    bool overflowed = false;
    std::size_t first_visible_index = 0;
    std::size_t visible_capacity = 0;
};

struct TabHitResult {
    TabHitRegion region = TabHitRegion::None;
    std::size_t index = 0;
};

[[nodiscard]] TabStripLayout CalculateTabStripLayout(
    SIZE client_size,
    std::size_t tab_count,
    UINT dpi = USER_DEFAULT_SCREEN_DPI,
    int maximum_tab_width_pixels = 240,
    std::size_t first_visible_index = 0);
[[nodiscard]] RECT CalculateConfiguredTabStripBounds(
    const RECT& group_bounds,
    int tab_strip_height,
    TabStripAlignment alignment,
    int width_percent) noexcept;
[[nodiscard]] TabHitResult HitTestTabStrip(
    const TabStripLayout& layout,
    POINT point) noexcept;
[[nodiscard]] std::size_t CalculateRelativeTabIndex(
    std::size_t current_index,
    std::size_t tab_count,
    int direction) noexcept;

}  // namespace ctm
