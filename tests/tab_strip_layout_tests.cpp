#include "tab_strip_layout.h"

#include <Windows.h>

#include <cstddef>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] POINT Center(const RECT& rectangle) noexcept {
    return {
        .x = rectangle.left + (rectangle.right - rectangle.left) / 2,
        .y = rectangle.top + (rectangle.bottom - rectangle.top) / 2,
    };
}

void TestOneTabBodyAndCloseHit() {
    const ctm::TabStripLayout layout =
        ctm::CalculateTabStripLayout({.cx = 900, .cy = 38}, 1);
    Expect(layout.items.size() == 1,
           "one member should produce one layout item");
    Expect(layout.items[0].bounds.right - layout.items[0].bounds.left <= 240,
           "one wide tab should respect the maximum width");
    Expect(layout.items[0].bounds.bottom == layout.client_size.cy,
           "the shaped tab should touch Chrome at the lower window edge");

    const ctm::TabHitResult body =
        ctm::HitTestTabStrip(layout, {
                                        .x = layout.items[0].bounds.left + 8,
                                        .y = Center(layout.items[0].bounds).y,
                                    });
    Expect(body.region == ctm::TabHitRegion::Body && body.index == 0,
           "the title area should hit the first tab body");
    const ctm::TabHitResult close = ctm::HitTestTabStrip(
        layout, Center(layout.items[0].close_bounds));
    Expect(close.region == ctm::TabHitRegion::Close && close.index == 0,
           "the close rectangle should take precedence over the tab body");
}

void TestThreeTabsAreOrderedAndSeparated() {
    const ctm::TabStripLayout layout =
        ctm::CalculateTabStripLayout({.cx = 720, .cy = 38}, 3);
    Expect(layout.items.size() == 3,
           "three members should produce three layout items");
    for (std::size_t index = 1; index < layout.items.size(); ++index) {
        Expect(layout.items[index - 1].bounds.right <
                   layout.items[index].bounds.left,
               "adjacent tabs should retain a non-overlapping gap");
    }
    const ctm::TabHitResult middle =
        ctm::HitTestTabStrip(layout, {
                                        .x = layout.items[1].bounds.left + 6,
                                        .y = Center(layout.items[1].bounds).y,
                                    });
    Expect(middle.region == ctm::TabHitRegion::Body && middle.index == 1,
           "the middle tab body should map to index one");

    const POINT gap = {
        .x = layout.items[0].bounds.right,
        .y = Center(layout.items[0].bounds).y,
    };
    Expect(ctm::HitTestTabStrip(layout, gap).region ==
               ctm::TabHitRegion::None,
           "the gap between tabs should not activate either neighbor");
}

void TestFiveNarrowTabsUseScrollableOverflow() {
    const ctm::TabStripLayout first =
        ctm::CalculateTabStripLayout({.cx = 260, .cy = 38}, 5);
    Expect(first.items.size() == 5 && first.overflowed &&
               first.first_visible_index == 0 &&
               first.visible_capacity == 3,
           "five members in a narrow strip should expose a three-tab viewport");
    for (std::size_t index = 0; index < first.visible_capacity; ++index) {
        const ctm::TabLayoutItem& item = first.items[index];
        const ctm::TabHitResult title =
            ctm::HitTestTabStrip(first, {
                                            .x = item.bounds.left + 2,
                                            .y = Center(item.bounds).y,
                                        });
        Expect(title.region == ctm::TabHitRegion::Body &&
                   title.index == index,
               "every initially visible overflow tab should retain a body hit target");
        const ctm::TabHitResult close =
            ctm::HitTestTabStrip(first, Center(item.close_bounds));
        Expect(close.region == ctm::TabHitRegion::Close &&
                   close.index == index,
               "every initially visible overflow tab should retain its close region");
    }
    Expect(ctm::HitTestTabStrip(
               first, Center(first.items[3].bounds)).region ==
               ctm::TabHitRegion::None,
           "an item outside the first overflow viewport must not be clickable");

    const ctm::TabStripLayout last = ctm::CalculateTabStripLayout(
        {.cx = 260, .cy = 38}, 5, 96, 240, 2);
    Expect(last.overflowed && last.first_visible_index == 2 &&
               last.visible_capacity == 3,
           "scrolling should reveal the final three tabs without changing capacity");
    for (std::size_t index = 2; index < 5; ++index) {
        const ctm::TabHitResult title = ctm::HitTestTabStrip(
            last, Center(last.items[index].bounds));
        Expect(title.region == ctm::TabHitRegion::Body &&
                   title.index == index,
               "every final overflow tab should become clickable after scrolling");
    }
    Expect(ctm::HitTestTabStrip(
               last, Center(last.items[1].bounds)).region ==
               ctm::TabHitRegion::None,
           "a tab before the scrolled viewport must not remain clickable");
}

void TestInvalidAndOutsideGeometryIsRejected() {
    Expect(ctm::CalculateTabStripLayout({.cx = 100, .cy = 38}, 0)
               .items.empty(),
           "zero members should produce no tabs");
    Expect(ctm::CalculateTabStripLayout({.cx = 8, .cy = 38}, 5)
               .items.empty(),
           "an impossibly narrow strip should be rejected");
    const ctm::TabStripLayout layout =
        ctm::CalculateTabStripLayout({.cx = 500, .cy = 38}, 3);
    Expect(ctm::HitTestTabStrip(layout, {.x = -1, .y = -1}).region ==
               ctm::TabHitRegion::None,
           "a point outside the client should hit nothing");
}

void TestDpiScaledLayout() {
    const ctm::TabStripLayout at_100 =
        ctm::CalculateTabStripLayout({.cx = 900, .cy = 38}, 3, 96);
    const ctm::TabStripLayout at_200 =
        ctm::CalculateTabStripLayout({.cx = 1800, .cy = 76}, 3, 192);
    Expect(at_100.items.size() == 3 && at_200.items.size() == 3,
           "supported DPI values should retain all tab hit targets");
    if (at_100.items.size() == 3 && at_200.items.size() == 3) {
        const int close_100 = at_100.items[0].close_bounds.right -
                              at_100.items[0].close_bounds.left;
        const int close_200 = at_200.items[0].close_bounds.right -
                              at_200.items[0].close_bounds.left;
        Expect(close_200 == close_100 * 2,
               "the close target should double at 200 percent DPI");
    }
}

void TestConfiguredTabWidthAndStripAlignment() {
    const ctm::TabStripLayout configured = ctm::CalculateTabStripLayout(
        {.cx = 900, .cy = 38}, 3, 96, 180);
    Expect(configured.items.size() == 3 &&
               configured.items[0].bounds.right -
                       configured.items[0].bounds.left <=
                   180,
           "the configured logical tab width should cap each tab");

    constexpr RECT group{100, 50, 1100, 750};
    const RECT left = ctm::CalculateConfiguredTabStripBounds(
        group, 38, ctm::TabStripAlignment::Left, 60);
    const RECT center = ctm::CalculateConfiguredTabStripBounds(
        group, 38, ctm::TabStripAlignment::Center, 60);
    const RECT right = ctm::CalculateConfiguredTabStripBounds(
        group, 38, ctm::TabStripAlignment::Right, 60);
    Expect(left.left == 100 && left.right == 700,
           "left alignment should anchor the configured width to the group left");
    Expect(center.left == 300 && center.right == 900,
           "center alignment should distribute unused width equally");
    Expect(right.left == 500 && right.right == 1100,
           "right alignment should anchor the configured width to the group right");
    Expect(left.top == 50 && left.bottom == 88 &&
               center.top == left.top && right.bottom == left.bottom,
           "alignment should not change the configured strip height");
}

void TestV31NormalStripAttachesAboveChrome() {
    constexpr RECT chrome{100, 88, 1100, 750};
    const RECT left = ctm::CalculateV31TabStripBounds(
        chrome,
        38,
        ctm::TabStripSurfaceMode::AttachedAbove,
        ctm::TabStripAlignment::Left,
        60);
    const RECT center = ctm::CalculateV31TabStripBounds(
        chrome,
        38,
        ctm::TabStripSurfaceMode::AttachedAbove,
        ctm::TabStripAlignment::Center,
        60);
    const RECT right = ctm::CalculateV31TabStripBounds(
        chrome,
        38,
        ctm::TabStripSurfaceMode::AttachedAbove,
        ctm::TabStripAlignment::Right,
        60);
    Expect(left.left == 100 && left.right == 700,
           "a left-aligned attached strip should retain V2 positioning");
    Expect(center.left == 300 && center.right == 900,
           "a centered attached strip should retain V2 positioning");
    Expect(right.left == 500 && right.right == 1100,
           "a right-aligned attached strip should retain V2 positioning");
    Expect(left.top == 50 && left.bottom == chrome.top &&
               center.top == left.top && right.bottom == left.bottom,
           "a normal V3.1 strip should touch Chrome without overlapping it");
}

void TestV31MaximizedOverlayReservesCaptionControls() {
    constexpr RECT chrome{0, 0, 1920, 1040};
    const RECT right = ctm::CalculateV31TabStripBounds(
        chrome,
        38,
        ctm::TabStripSurfaceMode::MaximizedOverlay,
        ctm::TabStripAlignment::Right,
        60,
        140);
    Expect(right.left == 628 && right.right == 1780,
           "a maximized right-aligned strip should end before caption controls");
    Expect(right.top == 0 && right.bottom == 38,
           "a maximized strip should overlay rather than reserve a new row");

    const RECT full_width = ctm::CalculateV31TabStripBounds(
        chrome,
        38,
        ctm::TabStripSurfaceMode::MaximizedOverlay,
        ctm::TabStripAlignment::Right,
        100,
        140);
    Expect(full_width.left == 0 && full_width.right == 1780,
           "an oversized configured strip should clamp to the caption-safe area");
}

void TestV31PlacementSupportsNegativeCoordinatesAndDpiScaling() {
    constexpr RECT secondary{-2560, 0, 0, 1440};
    const RECT overlay = ctm::CalculateV31TabStripBounds(
        secondary,
        76,
        ctm::TabStripSurfaceMode::MaximizedOverlay,
        ctm::TabStripAlignment::Right,
        50,
        280);
    Expect(overlay.left == -1560 && overlay.right == -280 &&
               overlay.top == 0 && overlay.bottom == 76,
           "the overlay model should preserve negative monitor coordinates and scaled inputs");

    const RECT invalid = ctm::CalculateV31TabStripBounds(
        {.left = 0, .top = 0, .right = 0, .bottom = 100},
        38,
        ctm::TabStripSurfaceMode::MaximizedOverlay,
        ctm::TabStripAlignment::Right,
        60,
        140);
    Expect(invalid.right == invalid.left && invalid.bottom == invalid.top,
           "invalid Chrome bounds should not produce a V3.1 strip");
}

void TestCaptionControlReserveUsesDpiAwareSystemInputs() {
    Expect(ctm::CalculateCaptionControlReserveWidth(36, 22, 4, 4) == 148,
           "the 100-percent reserve should conservatively fit three Chromium controls");
    Expect(ctm::CalculateCaptionControlReserveWidth(72, 44, 8, 8) == 296,
           "the caption-control reserve should scale linearly at 200 percent DPI");
    Expect(ctm::CalculateCaptionControlReserveWidth(-1, -1, -1, -1) == 0,
           "invalid system metrics should clamp safely");
}

void TestTabContentFollowsConfiguredAlignment() {
    const ctm::TabStripLayout left = ctm::CalculateTabStripLayout(
        {.cx = 900, .cy = 38},
        2,
        96,
        180,
        0,
        ctm::TabStripAlignment::Left);
    const ctm::TabStripLayout center = ctm::CalculateTabStripLayout(
        {.cx = 900, .cy = 38},
        2,
        96,
        180,
        0,
        ctm::TabStripAlignment::Center);
    const ctm::TabStripLayout right = ctm::CalculateTabStripLayout(
        {.cx = 900, .cy = 38},
        2,
        96,
        180,
        0,
        ctm::TabStripAlignment::Right);
    Expect(left.items.size() == 2 && center.items.size() == 2 &&
               right.items.size() == 2,
           "configured content alignment should retain every tab");
    if (left.items.size() == 2 && center.items.size() == 2 &&
        right.items.size() == 2) {
        Expect(left.items.front().bounds.left == 0 &&
                   right.items.back().bounds.right == 900,
               "few tabs should pack against the configured outer edge");
        Expect(center.items.front().bounds.left -
                       left.items.front().bounds.left ==
                   (right.items.front().bounds.left -
                    left.items.front().bounds.left) /
                       2,
               "centered tab content should split unused width evenly");
    }
}

void TestCompactStripUsesConfiguredWidthAsMaximum() {
    constexpr RECT available{100, 20, 1100, 50};
    const RECT left = ctm::CalculateCompactTabStripBounds(
        available, 2, 96, 180, ctm::TabStripAlignment::Left);
    const RECT center = ctm::CalculateCompactTabStripBounds(
        available, 2, 96, 180, ctm::TabStripAlignment::Center);
    const RECT right = ctm::CalculateCompactTabStripBounds(
        available, 2, 96, 180, ctm::TabStripAlignment::Right);
    Expect(left.left == 100 && left.right == 461 &&
               center.left == 419 && center.right == 780 &&
               right.left == 739 && right.right == 1100,
           "two tabs should form a compact 361-pixel group at each configured edge");

    const RECT constrained = ctm::CalculateCompactTabStripBounds(
        {.left = -800, .top = 10, .right = -500, .bottom = 48},
        5,
        192,
        180,
        ctm::TabStripAlignment::Right);
    Expect(constrained.left == -800 && constrained.right == -500 &&
               constrained.top == 10 && constrained.bottom == 48,
           "an overflowing high-DPI tab group should retain the full available viewport");
}

void TestInvisibleOwnerFrameDoesNotExtendTabStrip() {
    constexpr RECT owner_window{100, 60, 1100, 760};
    constexpr RECT owner_visible{107, 60, 1093, 753};
    const RECT right = ctm::AdjustTabStripBoundsForInvisibleFrame(
        {.left = 500, .top = 34, .right = 1100, .bottom = 60},
        owner_window,
        owner_visible);
    Expect(right.left == 500 && right.right == 1093,
           "a right-aligned strip should stop at the owner's visible frame instead of its invisible resize border");

    const RECT left = ctm::AdjustTabStripBoundsForInvisibleFrame(
        {.left = 100, .top = 34, .right = 700, .bottom = 60},
        owner_window,
        owner_visible);
    Expect(left.left == 107 && left.right == 700,
           "a left-aligned strip should start at the owner's visible frame");

    const RECT maximized = ctm::AdjustTabStripBoundsForInvisibleFrame(
        {.left = 0, .top = 0, .right = 1780, .bottom = 26},
        {.left = -8, .top = -8, .right = 1928, .bottom = 1048},
        {.left = 0, .top = 0, .right = 1920, .bottom = 1040});
    Expect(maximized.left == 0 && maximized.right == 1780,
           "an already caption-safe maximized overlay should remain unchanged");
}

void TestRelativeKeyboardNavigationWraps() {
    Expect(ctm::CalculateRelativeTabIndex(0, 3, 1) == 1 &&
               ctm::CalculateRelativeTabIndex(1, 3, 1) == 2 &&
               ctm::CalculateRelativeTabIndex(2, 3, 1) == 0,
           "forward keyboard navigation should wrap after the final tab");
    Expect(ctm::CalculateRelativeTabIndex(2, 3, -1) == 1 &&
               ctm::CalculateRelativeTabIndex(1, 3, -1) == 0 &&
               ctm::CalculateRelativeTabIndex(0, 3, -1) == 2,
           "backward keyboard navigation should wrap before the first tab");
    Expect(ctm::CalculateRelativeTabIndex(8, 3, 0) == 2 &&
               ctm::CalculateRelativeTabIndex(0, 0, 1) == 0,
           "keyboard navigation should handle stale and empty indices safely");
}

}  // namespace

int main() {
    TestOneTabBodyAndCloseHit();
    TestThreeTabsAreOrderedAndSeparated();
    TestFiveNarrowTabsUseScrollableOverflow();
    TestInvalidAndOutsideGeometryIsRejected();
    TestDpiScaledLayout();
    TestConfiguredTabWidthAndStripAlignment();
    TestV31NormalStripAttachesAboveChrome();
    TestV31MaximizedOverlayReservesCaptionControls();
    TestV31PlacementSupportsNegativeCoordinatesAndDpiScaling();
    TestCaptionControlReserveUsesDpiAwareSystemInputs();
    TestTabContentFollowsConfiguredAlignment();
    TestCompactStripUsesConfiguredWidthAsMaximum();
    TestInvisibleOwnerFrameDoesNotExtendTabStrip();
    TestRelativeKeyboardNavigationWraps();

    if (failures != 0) {
        std::cerr << failures << " tab-strip layout test(s) failed.\n";
        return 1;
    }
    std::cout << "All tab-strip layout tests passed.\n";
    return 0;
}
