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

TEST_CASE("document: undo steps") {
    UndoHistory h;
    Document d;
    d.siteswap = "531";
    h.recorded = d;
    CHECK(!recordStep(h, d));  // nothing changed
    // The camera isn't a change.
    Document moved = d;
    moved.hasCamera = true;
    moved.camera[0] = 2.0;
    CHECK(!recordStep(h, moved));
    // Two changes, two steps, each named by what it did (or as asked).
    Document a = d;
    a.tempo = 160.0;
    REQUIRE(recordStep(h, a));
    CHECK_EQ(h.undo.back().label, std::string("Tempo 150 to 160"));
    Document b = a;
    b.siteswap = "441";
    REQUIRE(recordStep(h, b, "Load \"Box\""));
    CHECK_EQ(h.undo.back().label, std::string("Load \"Box\""));
    CHECK_EQ(h.undo.size(), 2u);
    // Undo: back a step at a time, keeping the camera there is now.
    Document now = b;
    now.hasCamera = true;
    now.camera[3] = 45.0;
    Document show;
    std::string label;
    REQUIRE(undoStep(h, now, &show, &label));
    CHECK_EQ(label, std::string("Load \"Box\""));
    CHECK(show.sameForUndo(a));
    CHECK(show.hasCamera);
    CHECK_EQ(show.camera[3], 45.0);
    REQUIRE(undoStep(h, show, &show, &label));
    CHECK(show.sameForUndo(d));
    CHECK(!undoStep(h, show, &show, &label));
    // Redo, and a new step ends redo.
    REQUIRE(redoStep(h, show, &show, &label));
    CHECK(show.sameForUndo(a));
    CHECK_EQ(h.redo.size(), 1u);
    Document c = show;
    c.dwell = 1.8;
    REQUIRE(recordStep(h, c));
    CHECK(h.redo.empty());
    CHECK_EQ(h.undo.back().label, std::string("Dwell 1.60 to 1.80"));
}

TEST_CASE("document: what changed, by name") {
    Document a;
    a.siteswap = "531";
    Document b = a;
    b.siteswap = "441";
    CHECK_EQ(describeChange(a, b), std::string("Throws 531 to 441"));
    b = a;
    b.count = 4;
    b.prop = PropType::Club;
    CHECK_EQ(describeChange(a, b), std::string("4-count, Props: Clubs"));
    b = a;
    b.distance = 1.8;
    CHECK_EQ(describeChange(a, b), std::string("Distance auto to 1.80 m"));

    // The choreography: marks, keyframes, links, its length.
    const LibraryPattern* p = findPattern("Salad Spinner");
    REQUIRE(p != nullptr);
    Document s = documentFor(*p);
    REQUIRE(s.choreography.marks.size() > 2);
    Document t = s;
    t.choreography.marks[0].x += 0.5f;
    CHECK_EQ(describeChange(s, t), "Move mark " + s.choreography.marks[0].name);
    t = s;
    t.choreography.marks[1].yaw += 1.0f;
    CHECK_EQ(describeChange(s, t), "Turn mark " + s.choreography.marks[1].name);
    t = s;
    t.choreography.marks[2].name = "zz";
    CHECK_EQ(describeChange(s, t), "Rename mark " + s.choreography.marks[2].name + " to zz");
    t = s;
    SpikeMark m;
    m.name = "y1";
    t.choreography.marks.push_back(m);
    CHECK_EQ(describeChange(s, t), std::string("Add mark y1"));
    t = s;
    Keyframe k = t.choreography.keys[1][0];
    k.beat = 2;
    setKeyframe(&t.choreography, 1, k);
    CHECK_EQ(describeChange(s, t), std::string("J2: keyframe on beat 3"));
    CHECK_EQ(describeChange(t, s), std::string("J2: delete keyframe on beat 3"));
    t = s;
    WalkLink l;
    l.leader = 0;
    setWalkLink(&t.choreography, 2, l);
    CHECK_EQ(describeChange(s, t), std::string("J3 walks like J1"));
    t = s;
    t.choreography.cycle = 12;
    CHECK(describeChange(s, t).find("Choreography length 24 to 12") != std::string::npos);
    t = s;
    t.choreography = Choreography();
    CHECK_EQ(describeChange(s, t), std::string("Clear choreography"));
}

TEST_CASE("document: a juggler put on a new mark") {
    const LibraryPattern* p = findPattern("Salad Spinner");
    REQUIRE(p != nullptr);
    const Document s = documentFor(*p);
    Document t = s;
    SpikeMark m;
    m.name = "y1";
    t.choreography.marks.push_back(m);
    Keyframe k;
    k.beat = 2;
    k.mark = static_cast<int>(t.choreography.marks.size()) - 1;
    setKeyframe(&t.choreography, 2, k);
    CHECK_EQ(describeChange(s, t), std::string("Add mark y1, J3: keyframe on beat 3"));
}

TEST_CASE("document: a new history (a pattern loaded)") {
    UndoHistory h;
    Document a;
    a.siteswap = "531";
    h.recorded = a;
    Document b = a;
    b.tempo = 120.0;
    REQUIRE(recordStep(h, b));
    Document shown;
    std::string label;
    REQUIRE(undoStep(h, b, &shown, &label));
    Document loaded;
    loaded.siteswap = "441";
    startHistory(h, loaded);
    CHECK(h.undo.empty());
    CHECK(h.redo.empty());
    CHECK(!undoStep(h, loaded, &shown, &label));
    CHECK(!recordStep(h, loaded));
}
