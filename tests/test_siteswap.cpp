// Reading siteswaps: solo and passing patterns, sketches, and the errors.
#include "check.h"
#include "siteswap.h"

#include <string>

namespace {
Siteswap parse(const std::string& text) { return parseSiteswap(text); }
}  // namespace

TEST_CASE("siteswap: solo patterns and their prop counts") {
    struct Case {
        const char* text;
        int props;
    };
    for (const Case& c : {Case{"3", 3}, Case{"531", 3}, Case{"441", 3}, Case{"51", 3}, Case{"5551", 4},
                          Case{"7441", 4}, Case{"97531", 5}, Case{"b", 11}, Case{"50505", 3}, Case{"7070707", 4}}) {
        const Siteswap s = parse(c.text);
        CHECK_MESSAGE(s.valid, c.text << ": " << s.error);
        CHECK_MESSAGE(s.ballCount == c.props, c.text << " has " << s.ballCount << " props");
        CHECK_MESSAGE(s.loop.jugglers == 1, c.text);
    }
}

TEST_CASE("siteswap: collisions are errors, naming the throws") {
    for (const char* text : {"432", "54", "3 4", "6 3"}) {
        const Siteswap s = parse(text);
        CHECK_MESSAGE(!s.valid, text);
        CHECK_MESSAGE(!s.error.empty(), text);
    }
    const Siteswap s = parse("432");
    CHECK(!s.problemThrows.empty());
}

TEST_CASE("siteswap: an average that isn't whole is an error") {
    const Siteswap s = parse("43");
    CHECK(!s.valid);
    CHECK(!s.error.empty());
}

TEST_CASE("siteswap: passing patterns") {
    const Siteswap fourCount = parse("<3p 3 3 3|3p 3 3 3>");
    CHECK_MESSAGE(fourCount.valid, fourCount.error);
    CHECK_EQ(fourCount.loop.jugglers, 2);
    CHECK_EQ(fourCount.ballCount, 6);
    CHECK_EQ(fourCount.loop.period, 4);

    const Siteswap feed = parse("<3p2 3 3p3 3|3p1 3 3 3|3 3 3p1 3>");
    CHECK_MESSAGE(feed.valid, feed.error);
    CHECK_EQ(feed.loop.jugglers, 3);
    CHECK_EQ(feed.ballCount, 9);
    // J1's pass on beat 1 goes to J2.
    CHECK_EQ(feed.loop.at(0, 0).dest, 1);

    const Siteswap relative = parse("<3p+1 3|3p+1 3|3p+1 3>");
    CHECK_MESSAGE(relative.valid, relative.error);
    CHECK_EQ(relative.loop.at(2, 0).dest, 0);  // (round the ring)
}

TEST_CASE("siteswap: a broken passing pattern is an error") {
    const Siteswap s = parse("<3p 3|3 3>");
    CHECK(!s.valid);
    CHECK(!s.error.empty());
}

TEST_CASE("siteswap: sketches (open throws) aren't valid, but aren't errors either") {
    const Siteswap s = parse("<3p ? 3 ?|? 3 ? 3>");
    CHECK(!s.valid);
    CHECK(s.sketch);
    CHECK(s.error.empty());
    CHECK_EQ(s.openThrows, 4);
}

TEST_CASE("siteswap: hands swapped and links") {
    const Siteswap swapped = parse("<4p2 3 4p3 3|3 4p1 3 3,LRswap|3 3 3 4p1,LRswap>");
    CHECK_MESSAGE(swapped.valid, swapped.error);
    CHECK_EQ(swapped.ballCount, 10);
}
