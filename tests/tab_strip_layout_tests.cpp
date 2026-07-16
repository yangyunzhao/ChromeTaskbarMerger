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

void TestFiveNarrowTabsRemainHittable() {
    const ctm::TabStripLayout layout =
        ctm::CalculateTabStripLayout({.cx = 260, .cy = 38}, 5);
    Expect(layout.items.size() == 5,
           "five members should fit in the supported narrow strip");
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        const ctm::TabLayoutItem& item = layout.items[index];
        const ctm::TabHitResult title =
            ctm::HitTestTabStrip(layout, {
                                            .x = item.bounds.left + 2,
                                            .y = Center(item.bounds).y,
                                        });
        Expect(title.region == ctm::TabHitRegion::Body &&
                   title.index == index,
               "every narrow tab should retain a body hit target");
        const ctm::TabHitResult close =
            ctm::HitTestTabStrip(layout, Center(item.close_bounds));
        Expect(close.region == ctm::TabHitRegion::Close &&
                   close.index == index,
               "every supported narrow tab should retain its close region");
    }
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

}  // namespace

int main() {
    TestOneTabBodyAndCloseHit();
    TestThreeTabsAreOrderedAndSeparated();
    TestFiveNarrowTabsRemainHittable();
    TestInvalidAndOutsideGeometryIsRejected();

    if (failures != 0) {
        std::cerr << failures << " tab-strip layout test(s) failed.\n";
        return 1;
    }
    std::cout << "All tab-strip layout tests passed.\n";
    return 0;
}
