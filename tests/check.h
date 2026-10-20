// check.h - a tiny unit-test framework for JuggleSim's tests.
//
// It's a stand-in for doctest (https://github.com/doctest/doctest), using the same names for
// the same things (TEST_CASE, CHECK, CHECK_MESSAGE, REQUIRE, ...), so the tests could move to
// doctest without changes. Nothing to install: the tests build from this header alone.
//
//     TEST_CASE("what it checks") {
//         CHECK(a == b);                    // reports and carries on
//         CHECK_MESSAGE(ok, "while " << x); // ...with a note
//         REQUIRE(p != nullptr);            // reports and ends this test case
//     }
//
// A test case for a known problem, not fixed yet, can be marked so its failures are reported
// as warnings and don't fail the build (doctest's may_fail):
//
//     TEST_CASE("what it checks" * doctest::may_fail()) { ... }
//
// Failures are printed as "file(line): error: ..." (warnings for may_fail), the form Visual
// Studio's Error List and most editors pick up.
//
// test_main.cpp runs them all (or those whose name contains the first argument).
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct TestCase {
    const char* name;
    void (*run)();
    bool mayFail;
};
// A test case's name with its decorations (see doctest::may_fail).
struct Name {
    const char* text;
    bool mayFail;
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}
struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run, false}); }
    Registrar(Name name, void (*run)()) { registry().push_back({name.text, run, name.mayFail}); }
};
struct Failed {};  // thrown by REQUIRE to end a test case

// Failures in the test case being run (reset for each).
inline int& failures() {
    static int n = 0;
    return n;
}
// Whether the test case being run is a may_fail one.
inline bool& mayFail() {
    static bool on = false;
    return on;
}
// A known problem (may_fail) reports its first few failures, so it doesn't flood every build.
constexpr int kKnownProblemReports = 3;
inline void fail(const char* file, int line, const char* what, const std::string& message) {
    ++failures();
    if (mayFail() && failures() > kKnownProblemReports) return;
    std::printf("%s(%d): %s: check failed: %s%s%s\n", file, line, mayFail() ? "warning" : "error", what,
                message.empty() ? "" : " -- ", message.c_str());
}

}  // namespace check

namespace doctest {
struct may_fail {};
inline check::Name operator*(const char* name, may_fail) { return {name, true}; }
}  // namespace doctest

#define CHECK_CONCAT2(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT2(a, b)
#define TEST_CASE(name)                                                                              \
    static void CHECK_CONCAT(checkTest_, __LINE__)();                                                \
    static check::Registrar CHECK_CONCAT(checkRegistrar_, __LINE__)(name, &CHECK_CONCAT(checkTest_, __LINE__)); \
    static void CHECK_CONCAT(checkTest_, __LINE__)()

#define CHECK_MESSAGE(expr, msg)                                                  \
    do {                                                                          \
        if (!(expr)) {                                                            \
            std::ostringstream checkMessage_;                                     \
            checkMessage_ << msg;                                                 \
            check::fail(__FILE__, __LINE__, #expr, checkMessage_.str());          \
        }                                                                         \
    } while (false)
#define CHECK(expr) CHECK_MESSAGE(expr, "")
#define REQUIRE_MESSAGE(expr, msg)                                                \
    do {                                                                          \
        if (!(expr)) {                                                            \
            std::ostringstream checkMessage_;                                     \
            checkMessage_ << msg;                                                 \
            check::fail(__FILE__, __LINE__, #expr, checkMessage_.str());          \
            throw check::Failed();                                                \
        }                                                                         \
    } while (false)
#define REQUIRE(expr) REQUIRE_MESSAGE(expr, "")
#define CHECK_EQ(a, b) CHECK_MESSAGE((a) == (b), #a " is " << (a) << ", " #b " is " << (b))
#define CHECK_LE(a, b) CHECK_MESSAGE((a) <= (b), #a " is " << (a) << ", " #b " is " << (b))
#define CHECK_GE(a, b) CHECK_MESSAGE((a) >= (b), #a " is " << (a) << ", " #b " is " << (b))
