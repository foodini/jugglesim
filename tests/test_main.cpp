// test_main.cpp - runs JuggleSim's tests (see check.h): all of them, or with an argument, only
// those whose names contain it. Exits 0 if everything passed, 1 if anything failed (known
// problems, may_fail test cases, are reported but don't count).
#include "check.h"

#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0, known = 0;
    for (const check::TestCase& test : check::registry()) {
        if (only && !std::strstr(test.name, only)) continue;
        ++run;
        check::failures() = 0;
        check::mayFail() = test.mayFail;
        try {
            test.run();
        } catch (const check::Failed&) {
        } catch (const std::exception& e) {
            check::fail(__FILE__, __LINE__, "exception", e.what());
        }
        if (check::failures() > 0 && test.mayFail) {
            ++known;
            if (check::failures() > check::kKnownProblemReports)
                std::printf("  (and %d more)\n", check::failures() - check::kKnownProblemReports);
            std::printf("KNOWN PROBLEM (doesn't fail the build): %s\n", test.name);
        } else if (check::failures() > 0) {
            ++failed;
            std::printf("FAILED: %s\n", test.name);
        }
    }
    std::printf("%d test case%s, %d failed", run, run == 1 ? "" : "s", failed);
    if (known > 0) std::printf(", %d known problem%s", known, known == 1 ? "" : "s");
    std::printf("\n");
    return failed > 0 ? 1 : 0;
}
