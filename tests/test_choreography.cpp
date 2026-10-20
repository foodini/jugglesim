// Choreography text: spike marks, keyframes and groups read and write consistently. (The walking
// loop's own structure isn't tested yet: it's being redesigned.)
#include "check.h"
#include "choreography.h"
#include "test_data.h"

TEST_CASE("choreography: every library choreography reads, and writes out stably") {
    int seen = 0;
    for (const LibraryPattern& p : library()) {
        if (!p.settings.hasChoreography) continue;
        ++seen;
        Choreography c;
        REQUIRE_MESSAGE(choreographyFromText(p.settings.choreography, &c), p.displayName());
        const std::string once = choreographyToText(c);
        Choreography again;
        REQUIRE_MESSAGE(choreographyFromText(once, &again), p.displayName() << ": " << once);
        CHECK_MESSAGE(choreographyToText(again) == once, p.displayName());
        CHECK_MESSAGE(again.marks.size() == c.marks.size(), p.displayName());
    }
    CHECK_GE(seen, 5);
}

TEST_CASE("choreography: marks, keyframes and groups") {
    Choreography c;
    REQUIRE(choreographyFromText("D2/C4/M1=01(0,1,0)/M2=02(0,-1,180)/J1(0:M1)/J2(0:M2)", &c));

    CHECK_EQ(static_cast<int>(c.marks.size()), 2);
    CHECK_EQ(c.marks[0].name, std::string("01"));
    CHECK(c.hasKeys(0));
    CHECK(c.hasKeys(1));
}

TEST_CASE("choreography: not a choreography") {
    Choreography c;
    CHECK(!choreographyFromText("hello", &c));
}
