// The document (document.h): comparing documents, and what deriveDocument works out from one,
// problems included.
#include "check.h"
#include "document.h"
#include "test_data.h"

namespace {

Document documentFor(const LibraryPattern& p) {
    Document d;
    d.siteswap = p.siteswap;
    if (p.settings.hasChoreography) choreographyFromText(p.settings.choreography, &d.choreography);
    if (p.settings.hasThrowNext) readThrowNext(p.settings.throwNext, &d.throwNext);
    return d;
}
const LibraryPattern* findPattern(const std::string& name) {
    for (const LibraryPattern& p : library())
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace

TEST_CASE("document: every library pattern works out with no problems") {
    int seen = 0;
    for (const LibraryPattern& p : library()) {
        const DerivedDocument d = deriveDocument(documentFor(p));
        CHECK_MESSAGE(d.ok(), p.displayName() << ": " << (d.problems.empty() ? "" : d.problems[0].text));
        CHECK_MESSAGE(!d.written.empty(), p.displayName());
        CHECK_MESSAGE(!d.played.loop.empty(), p.displayName());
        ++seen;
    }
    CHECK_GE(seen, 80);
}

TEST_CASE("document: throws with an error") {
    Document doc;
    doc.siteswap = "543";  // two props land together
    const DerivedDocument d = deriveDocument(doc);
    CHECK(!d.ok());
    REQUIRE(d.problem(DocumentProblemKind::Throws) != nullptr);
    CHECK(!d.problem(DocumentProblemKind::Throws)->text.empty());
    CHECK(d.written.empty());
    CHECK(d.played.loop.empty());
}

TEST_CASE("document: a sketch works out (it can be shown)") {
    Document doc;
    doc.siteswap = "<3p ? 3 3|3p 3 ? 3>";
    const DerivedDocument d = deriveDocument(doc);
    CHECK(d.ok());
    CHECK(d.parsed.sketch);
    CHECK_EQ(d.written.jugglers, 2);
}

TEST_CASE("document: a throwing loop that doesn't work") {
    Document doc;
    doc.siteswap = "<3p 3 3 3|3p 3 3 3>";
    doc.throwNext = {1, 1};  // both go on to T2's part; nobody to T1's
    const DerivedDocument d = deriveDocument(doc);
    CHECK(!d.ok());
    CHECK(d.problem(DocumentProblemKind::ThrowingLoop) != nullptr);
    CHECK(d.problem(DocumentProblemKind::Throws) == nullptr);
    CHECK(!d.written.empty());  // (the throws themselves are fine)
}

TEST_CASE("document: a walking loop that doesn't work") {
    const LibraryPattern* p = findPattern("Salad Spinner");
    REQUIRE(p != nullptr);
    Document doc = documentFor(*p);
    REQUIRE(doc.choreography.active());
    REQUIRE(deriveDocument(doc).ok());
    // J1 and J2 both go on as J3's part; nobody takes J1's.
    doc.choreography.next.resize(3);
    doc.choreography.next[0].role = 2;
    doc.choreography.next[1].role = 2;
    const DerivedDocument d = deriveDocument(doc);
    CHECK(!d.ok());
    CHECK(d.problem(DocumentProblemKind::WalkingLoop) != nullptr);
}

TEST_CASE("document: comparing") {
    Document a;
    a.siteswap = "531";
    Document b = a;
    CHECK(a == b);
    // A camera that isn't placed isn't compared...
    b.camera[0] = 3.0;
    CHECK(a == b);
    // ...one that is, is.
    a.hasCamera = b.hasCamera = true;
    CHECK(a != b);
    b = a;
    // Tempo, prop and dwell are part of the pattern (an unsaved change), but don't change what
    // it works out to.
    b.tempo = 120.0;
    b.prop = PropType::Club;
    CHECK(a != b);
    CHECK(a.derivesSameAs(b));
    b = a;
    b.siteswap = "441";
    CHECK(!a.derivesSameAs(b));
    b = a;
    b.throwNext = {0};
    CHECK(!a.derivesSameAs(b));
}
