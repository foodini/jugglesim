// The 3D mouse's response curves (response_curve.h): the dead zone, the ends, no overshoot, and
// the saved form (older ones with fewer points too), and the settings that hold them.
#include "check.h"
#include "response_curve.h"
#include "settings.h"

#include <cmath>

namespace {
constexpr int kLast = ResponseCurve::kPoints - 1;
}

TEST_CASE("response curve: nothing in the dead zone, the last rate past the end") {
    const ResponseCurve c{{12.0f, 22.0f, 31.0f, 49.0f, 61.0f, 69.0f}, {30.0f, 70.0f, 120.0f, 300.0f, 550.0f, 1000.0f}};
    CHECK_EQ(responseRate(c, 0.0f), 0.0f);
    CHECK_EQ(responseRate(c, c.x[0] - 0.5f), 0.0f);
    CHECK_EQ(responseRate(c, c.x[0]), c.y[0]);
    CHECK_EQ(responseRate(c, c.x[kLast]), c.y[kLast]);
    CHECK_EQ(responseRate(c, 1000.0f), c.y[kLast]);
    CHECK_EQ(responseRate(c, -c.x[kLast]), c.y[kLast]);  // (the size of the reading: the sign is the caller's)
}

TEST_CASE("response curve: goes through its points, and rises where they rise") {
    for (const ResponseCurve& c : {defaultSlideCurve(), defaultTurnCurve()}) {
        for (int i = 0; i < ResponseCurve::kPoints; ++i) CHECK(std::fabs(responseRate(c, c.x[i]) - c.y[i]) < 1e-3f);
        float last = 0.0f;
        for (float x = c.x[0]; x <= c.x[kLast]; x += 0.25f) {
            const float r = responseRate(c, x);
            CHECK_MESSAGE(r >= last - 1e-4f, "at " << x << ": " << r << " after " << last);
            last = r;
        }
    }
}

TEST_CASE("response curve: no overshoot where the points level off") {
    const ResponseCurve c{{10.0f, 50.0f, 100.0f, 150.0f, 200.0f, 300.0f},
                          {10.0f, 100.0f, 100.0f, 100.0f, 400.0f, 500.0f}};
    for (float x = 50.0f; x <= 150.0f; x += 1.0f) CHECK_MESSAGE(std::fabs(responseRate(c, x) - 100.0f) < 1e-3f, x);
    for (float x = 10.0f; x <= 50.0f; x += 1.0f) CHECK(responseRate(c, x) <= 100.0f + 1e-3f);
}

TEST_CASE("response curve: saved and read back") {
    const ResponseCurve c{{7.0f, 30.0f, 60.5f, 130.0f, 200.0f, 300.0f}, {2.5f, 20.0f, 90.0f, 250.0f, 550.0f, 1000.0f}};
    ResponseCurve back = defaultTurnCurve();
    REQUIRE(readResponseCurve(responseCurveText(c), &back));
    CHECK(back == c);
    ResponseCurve untouched = defaultTurnCurve();
    CHECK(!readResponseCurve("1:2", &untouched));                                // too few
    CHECK(!readResponseCurve("1:1 2:2 3:3 4:4 5:5 6:6 7:7", &untouched));        // too many
    CHECK(!readResponseCurve("5:1 4:2 6:3 7:4 8:5 9:6", &untouched));            // not increasing
    CHECK(!readResponseCurve("1:1 2:2 3:3 4:4 5:5 6:-5", &untouched));           // a negative rate
    CHECK(untouched == defaultTurnCurve());
}

TEST_CASE("response curve: one saved with fewer points keeps its shape") {
    ResponseCurve c = defaultSlideCurve();
    REQUIRE(readResponseCurve("41:30 110:120 172:300 213:550 300:1000", &c));
    CHECK_EQ(c.x[0], 41.0f);
    CHECK_EQ(c.x[kLast], 300.0f);
    CHECK_EQ(c.y[kLast], 1000.0f);
    // The point added (in the widest gap, 213-300) lies on the curve the five points made.
    for (int i = 1; i < ResponseCurve::kPoints; ++i) CHECK(c.x[i] > c.x[i - 1]);
    bool added = false;
    for (int i = 0; i < ResponseCurve::kPoints; ++i) added = added || c.x[i] == 257.0f;
    CHECK(added);
}

TEST_CASE("response curve: settings - full push, and older curves in counts") {
    // Older files: readings in raw counts (taken to run to 350), turned into percent.
    AppSettings old = settingsFromText("space_mouse_slide = 35:30 70:70 105:120 175:300 210:550 350:1000\n");
    CHECK(std::fabs(old.spaceSlideCurve.x[0] - 10.0f) < 1e-3f);
    CHECK(std::fabs(old.spaceSlideCurve.x[kLast] - 100.0f) < 1e-3f);
    CHECK_EQ(old.spaceSlideCurve.y[kLast], 1000.0f);
    CHECK(old.spaceTurnCurve == defaultTurnCurve());
    // A file with curves in percent ignores older ones, wherever they are.
    AppSettings both = settingsFromText(
        "space_mouse_turn = 35:3 70:6 105:9 175:22 210:40 350:72\n"
        "space_mouse_turn_curve = 5:1 10:2 20:4 40:8 60:16 80:32\n");
    CHECK_EQ(both.spaceTurnCurve.x[0], 5.0f);
    CHECK_EQ(both.spaceTurnCurve.y[kLast], 32.0f);
    // Full push: all six, or none.
    AppSettings full = settingsFromText("space_mouse_full_push = 5000 900 5500 1100 500 650\n");
    CHECK_EQ(full.spaceFullPush[1], 900.0f);
    CHECK_EQ(full.spaceFullPush[5], 650.0f);
    AppSettings bad = settingsFromText("space_mouse_full_push = 5000 900 5500\n");
    CHECK_EQ(bad.spaceFullPush[0], AppSettings().spaceFullPush[0]);
    // ...and saved the way it's read.
    CHECK(settingsFromText(settingsText(full)).spaceFullPush[3] == 1100.0f);
}
