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
    TestRelativeKeyboardNavigationWraps();

    if (failures != 0) {
        std::cerr << failures << " tab-strip layout test(s) failed.\n";
        return 1;
    }
    std::cout << "All tab-strip layout tests passed.\n";
    return 0;
}
