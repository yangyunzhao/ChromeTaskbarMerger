#pragma once

#include "app_config.h"

#include <Windows.h>

#include <cstddef>
#include <vector>

namespace ctm {

inline constexpr int kV2TabStripHeight = 38;
inline constexpr int kV31TabStripHeight = 26;

enum class TabHitRegion {
    None,
    Body,
    Close,
};

enum class TabStripSurfaceMode {
    AttachedAbove,
    MaximizedOverlay,
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
    std::size_t first_visible_index = 0,
    TabStripAlignment content_alignment = TabStripAlignment::Left);
[[nodiscard]] RECT CalculateConfiguredTabStripBounds(
    const RECT& group_bounds,
    int tab_strip_height,
    TabStripAlignment alignment,
    int width_percent) noexcept;
[[nodiscard]] RECT CalculateV31TabStripBounds(
    const RECT& chrome_bounds,
    int tab_strip_height,
    TabStripSurfaceMode surface_mode,
    TabStripAlignment alignment,
    int width_percent,
    int caption_control_reserve_width = 0) noexcept;
[[nodiscard]] int CalculateCaptionControlReserveWidth(
    int caption_button_width,
    int caption_button_height,
    int frame_width,
    int padded_border_width) noexcept;
[[nodiscard]] RECT CalculateCompactTabStripBounds(
    const RECT& available_bounds,
    std::size_t tab_count,
    UINT dpi,
    int maximum_tab_width_pixels,
    TabStripAlignment alignment) noexcept;
[[nodiscard]] RECT AdjustTabStripBoundsForInvisibleFrame(
    const RECT& available_bounds,
    const RECT& owner_window_bounds,
    const RECT& owner_visible_bounds) noexcept;
[[nodiscard]] TabHitResult HitTestTabStrip(
    const TabStripLayout& layout,
    POINT point) noexcept;
[[nodiscard]] std::size_t CalculateRelativeTabIndex(
    std::size_t current_index,
    std::size_t tab_count,
    int direction) noexcept;

}  // namespace ctm
