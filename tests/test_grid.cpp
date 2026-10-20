// The siteswap grid: text to grid and back, and editing columns.
#include "check.h"
#include "siteswap.h"
#include "siteswap_edit.h"

#include <string>

namespace {
// The pattern a grid's text reads as.
JugglingLoop loopOf(const std::string& text) { return parseSiteswap(text).loop; }
}  // namespace

TEST_CASE("grid: text to grid and back reads the same") {
    for (const char* text : {"3", "531", "<3p 3 3 3|3p 3 3 3>", "<3p2 3 3p3 3|3p1 3 3 3|3 3 3p1 3>",
                             "<3p+1 3|3p+1 3|3p+1 3>", "<3p ? 3 ?|? 3 ? 3>", "<4p2 3 4p3 3|3 4p1 3 3,LRswap|3 3 3 4p1,LRswap>"}) {
        SiteswapGrid grid;
        REQUIRE_MESSAGE(gridFromText(text, &grid), text);
        const std::string back = gridToText(grid);
        CHECK_MESSAGE(loopOf(back) == loopOf(text), text << " came back as " << back);
    }
}

TEST_CASE("grid: Delete Beat n and All After keeps the jugglers (regression)") {
    const std::string star =
        "<3p+2 3 3p+2 3 3p+2 3 ? 3 ? 3 ? ? ? ? ? ? ? ?|3p-1 3 3p-1 0 3p-1 0 ? 3 ? 3 ? ? ? ? ? ? ? ?|"
        "0 0 0 3 0 3 ? 3 ? 0 ? ? ? ? ? ? ? ?>";
    SiteswapGrid grid;
    REQUIRE(gridFromText(star, &grid));
    std::string why;
    REQUIRE(deleteColumns(&grid, 7, grid.period - 7, &why));
    CHECK_EQ(grid.period, 7);
    CHECK_EQ(grid.jugglers(), 3);
    const Siteswap after = parseSiteswap(gridToText(grid));
    CHECK_EQ(after.loop.jugglers, 3);
    CHECK_EQ(after.loop.period, 7);
}

TEST_CASE("grid: inserting and deleting columns") {
    SiteswapGrid grid;
    REQUIRE(gridFromText("<3p 3 3 3|3p 3 3 3>", &grid));
    insertColumns(&grid, 4, 4, GridCount(), false);
    CHECK_EQ(grid.period, 8);
    std::string why;
    REQUIRE(deleteColumns(&grid, 4, 4, &why));
    CHECK_EQ(grid.period, 4);
    CHECK(loopOf(gridToText(grid)) == loopOf("<3p 3 3 3|3p 3 3 3>"));
}

TEST_CASE("grid: a pattern can't lose its last beat") {
    SiteswapGrid grid;
    REQUIRE(gridFromText("531", &grid));
    std::string why;
    CHECK(!deleteColumns(&grid, 0, 3, &why));
}
