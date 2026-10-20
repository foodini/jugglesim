// The built-in pattern library (data/patterns.txt): every pattern in it reads, and survives a
// trip through the file format. (Each pattern added to the library is a new test.)
#include "check.h"
#include "siteswap.h"
#include "test_data.h"

TEST_CASE("library: data/patterns.txt is there and has patterns") {
    REQUIRE_MESSAGE(!libraryText().empty(), "run the tests from the repository root (data/patterns.txt not found)");
    CHECK_GE(static_cast<int>(library().size()), 50);
}

TEST_CASE("library: every pattern reads (a valid pattern or a sketch, never an error)") {
    for (const LibraryPattern& p : library()) {
        const Siteswap s = parseSiteswap(p.siteswap);
        CHECK_MESSAGE(s.valid || s.sketch, p.displayName() << ": " << s.error);
        CHECK_MESSAGE(p.jugglers > 0, p.displayName());
    }
}

TEST_CASE("library: every pattern survives being written out and read back") {
    for (const LibraryPattern& p : library()) {
        const std::vector<LibraryPattern> again = parsePatternLibrary(formatPatternLine(p));
        REQUIRE_MESSAGE(again.size() == 1, p.displayName());
        CHECK_MESSAGE(again[0].siteswap == p.siteswap, p.displayName());
        CHECK_MESSAGE(again[0].name == p.name, p.displayName());
        CHECK_MESSAGE(again[0].settings == p.settings, p.displayName() << ": " << formatPatternLine(p));
    }
}

TEST_CASE("library: settings are read") {
    const std::vector<LibraryPattern> p =
        parsePatternLibrary("<3p 3 3 3|3p 3 3 3> ; Test ; props=hoops tempo=135 dwell=1.2 distance=2.5 count=4\n");
    REQUIRE(p.size() == 1);
    CHECK(p[0].settings.hasProp);
    CHECK(p[0].settings.prop == PropType::Hoop);
    CHECK_EQ(p[0].settings.tempo, 135.0);
    CHECK_EQ(p[0].settings.dwell, 1.2);
    CHECK_EQ(p[0].settings.distance, 2.5);
    CHECK_EQ(p[0].settings.count, 4);
    CHECK_EQ(p[0].jugglers, 2);
}

TEST_CASE("library: every prop type has a name that reads back") {
    for (int i = 0; i < static_cast<int>(PropType::Count); ++i) {
        PropType prop = PropType::Ball;
        CHECK(propTypeFromKey(propTypeKey(static_cast<PropType>(i)), &prop));
        CHECK(prop == static_cast<PropType>(i));
    }
}

TEST_CASE("library: the default dwell is 1.6") {
    CHECK_EQ(PatternSettings().dwell, 1.6);
    CHECK_EQ(JuggleParams().dwellBeats, 1.6);
}
