// document.h - the pattern being edited, as one value (Document), and everything worked out
// from it (DerivedDocument): the throws read from the siteswap, the throwing and walking loops,
// what's thrown beat by beat, and anything wrong with it.
//
// A Document is plain data: two can be compared (that's what "unsaved changes" means: the
// document isn't the one last saved), and kept (the stage keeps the last one that worked, and
// plays that while the one being edited has a problem). deriveDocument is a pure function of it,
// so it can be tested without the app.
#pragma once

#include "choreography.h"
#include "juggle_sim.h"
#include "loop_ops.h"
#include "siteswap.h"

#include <string>
#include <vector>

struct Document {
    std::string siteswap;        // as typed (or written out by an edit)
    int count = 0;               // the grid's count directive ("4-count"), 0: none
    std::vector<int> throwNext;  // the throwing loop (the grid's next column; see loop_ops.h)
    Choreography choreography;   // none: Choreography() (not active)
    PropType prop = PropType::Ball;
    double tempo = 150.0;        // beats per minute
    double dwell = 1.6;          // beats
    double distance = 0.0;       // between passing jugglers (m); 0: automatic
    // A camera the user has placed (a free camera): position (m), yaw and pitch (degrees).
    bool hasCamera = false;
    double camera[5] = {0.0, 0.0, 0.0, 0.0, 0.0};

    bool operator==(const Document& o) const;
    bool operator!=(const Document& o) const { return !(*this == o); }
    // Whether deriveDocument gives the same for both: the same throws, throwing loop and
    // choreography (the rest is how they're shown).
    bool derivesSameAs(const Document& o) const;
    // Whether they're the same as far as undo goes: everything but the camera.
    bool sameForUndo(const Document& o) const;
};

// What can be wrong with a document. Any of them stops it being played.
enum class DocumentProblemKind {
    Throws,        // the siteswap has an error
    ThrowingLoop,  // the next column doesn't work (a part taken twice, props landing together)
    WalkingLoop,   // the choreography's walking loop doesn't work
};
struct DocumentProblem {
    DocumentProblemKind kind = DocumentProblemKind::Throws;
    std::string text;  // what's wrong ("J3's part is taken by J2 and J4; nobody takes J1's")
};

struct DerivedDocument {
    Siteswap parsed;       // the siteswap read
    JugglingLoop written;  // the throws as written (or the sketch); empty while they have an error
    ThrowLoopPlan plan;    // the throwing loop worked out
    WalkLoop walk;         // the walking loop worked out (one phase, valid, without a choreography)
    WalkPaths paths;       // ...as paths (empty unless it goes round more than once)
    PlayedLoop played;     // what's thrown, beat by beat, with both loops played out
    std::vector<DocumentProblem> problems;

    bool ok() const { return problems.empty(); }
    // The problem of that kind, or null.
    const DocumentProblem* problem(DocumentProblemKind kind) const;
};

DerivedDocument deriveDocument(const Document& doc);

// What changed from one document to another, as an undo step's name: "Tempo 150 to 160",
// "Move mark 0a", "J2's keyframes", "Throws 531 to 441"; two things at once joined ("4-count,
// throws..."). (A step can also be given a name of its own, as loading a pattern is.)
std::string describeChange(const Document& before, const Document& after);

// Undo and redo: steps, each the document before and after one action, with its name.
struct UndoStep {
    Document before, after;
    std::string label;
};
struct UndoHistory {
    std::vector<UndoStep> undo;
    std::vector<UndoStep> redo;
    Document recorded;  // the document as of the last step (what the next one starts from)
    static constexpr size_t kMaxSteps = 1000;
};
// Starts the history afresh at `now` (a pattern loaded or started: undo doesn't go back past it).
void startHistory(UndoHistory& h, const Document& now);
// Records a step if `now` differs from what was last recorded (camera aside): named `label`, or
// (if that's empty) by describeChange. A new step ends redo. Returns whether there was one.
bool recordStep(UndoHistory& h, const Document& now, const std::string& label = std::string());
// Undoes the last step: sets *show to the document to show (the step's before, with `now`'s
// camera), and *label to its name. False if there's nothing to undo. (Record any change made
// since the last step first, so it's what's undone.)
bool undoStep(UndoHistory& h, const Document& now, Document* show, std::string* label);
// ...and redoes the last step undone.
bool redoStep(UndoHistory& h, const Document& now, Document* show, std::string* label);
