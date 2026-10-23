// document.cpp - see document.h.

#include "document.h"

#include "pattern.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

bool Document::operator==(const Document& o) const {
    if (siteswap != o.siteswap || count != o.count || throwNext != o.throwNext || choreography != o.choreography ||
        prop != o.prop || tempo != o.tempo || dwell != o.dwell || distance != o.distance || hasCamera != o.hasCamera)
        return false;
    if (hasCamera)
        for (int i = 0; i < 5; ++i)
            if (camera[i] != o.camera[i]) return false;
    return true;
}

bool Document::derivesSameAs(const Document& o) const {
    return siteswap == o.siteswap && throwNext == o.throwNext && choreography == o.choreography;
}

const DocumentProblem* DerivedDocument::problem(DocumentProblemKind kind) const {
    for (const DocumentProblem& p : problems)
        if (p.kind == kind) return &p;
    return nullptr;
}

DerivedDocument deriveDocument(const Document& doc) {
    DerivedDocument d;
    d.parsed = parseSiteswap(doc.siteswap);
    if (d.parsed.sketch)
        d.written = d.parsed.loop;
    else if (d.parsed.valid)
        d.written = patternLoop(patternFromSiteswap(d.parsed));
    else
        d.problems.push_back({DocumentProblemKind::Throws, d.parsed.error});
    if (d.written.empty()) return d;

    // The walking loop: who walks whose part after the choreography's written beats.
    const Choreography& c = doc.choreography;
    if (c.active()) {
        d.walk = walkLoop(c, d.written.jugglers);
        if (!d.walk.valid) {
            d.problems.push_back({DocumentProblemKind::WalkingLoop, d.walk.problem});
        } else if (d.walk.phases > 1) {
            JuggleParams params;
            params.choreography = &c;
            d.paths.cycle = c.cycle > 0 ? c.cycle : choreographyCycle(d.written, params);
            d.paths.path = d.walk.role;
        }
    }

    // The throwing loop, played out: a pass goes to whoever walks its target's path when it lands.
    d.plan = throwLoopPlan(doc.throwNext, d.written.jugglers);
    d.played = playedLoop(d.written, d.plan.valid ? d.plan : ThrowLoopPlan(), d.paths);
    if (!d.plan.valid)
        d.problems.push_back({DocumentProblemKind::ThrowingLoop, d.plan.problem});
    else if (!d.played.ok)
        d.problems.push_back({DocumentProblemKind::ThrowingLoop, d.played.problem});
    return d;
}

bool Document::sameForUndo(const Document& o) const {
    return siteswap == o.siteswap && count == o.count && throwNext == o.throwNext && choreography == o.choreography &&
           prop == o.prop && tempo == o.tempo && dwell == o.dwell && distance == o.distance;
}

namespace {

std::string formatted(const char* format, double a, double b) {
    char text[96];
    std::snprintf(text, sizeof(text), format, a, b);
    return text;
}

std::string jugglerList(const std::vector<int>& jugglers) {
    std::string list;
    for (size_t i = 0; i < jugglers.size(); ++i)
        list += (i == 0 ? "" : (i + 1 == jugglers.size() ? " and " : ", ")) + std::string("J") +
                std::to_string(jugglers[i] + 1);
    return list;
}

bool sameKeyframe(const Keyframe& a, const Keyframe& b) {
    if (a.beat != b.beat || a.mark != b.mark || a.longTurn != b.longTurn) return false;
    return a.mark >= 0 || (a.x == b.x && a.z == b.z && a.yaw == b.yaw);  // (a mark's keyframe is where the mark is)
}

const std::vector<Keyframe>& keysOf(const Choreography& c, size_t j) {
    static const std::vector<Keyframe> none;
    return j < c.keys.size() ? c.keys[j] : none;
}

bool sameLink(const WalkLink& a, const WalkLink& b) {
    if (a.active() != b.active()) return false;
    return !a.active() || (a.leader == b.leader && a.offset == b.offset && a.turn == b.turn);
}

// What changed in the choreography (they differ).
void describeChoreographyChange(const Choreography& a, const Choreography& b, std::vector<std::string>* parts) {
    if (!a.active() && b.active()) {
        parts->push_back("Spike marks");
        return;
    }
    if (a.active() && !b.active()) {
        parts->push_back("Clear choreography");
        return;
    }
    // Spike marks: added, deleted; or renamed, moved, turned.
    const size_t before = a.marks.size(), after = b.marks.size();
    if (after != before) {
        const std::vector<SpikeMark>& more = after > before ? b.marks : a.marks;
        const std::vector<SpikeMark>& fewer = after > before ? a.marks : b.marks;
        std::vector<std::string> names;
        for (const SpikeMark& m : more) {
            bool found = false;
            for (const SpikeMark& f : fewer) found = found || f.name == m.name;
            if (!found) names.push_back(m.name);
        }
        const char* verb = after > before ? "Add" : "Delete";
        parts->push_back(names.size() == 1 ? std::string(verb) + " mark " + names[0]
                                           : std::string(verb) + " " + std::to_string(more.size() - fewer.size()) + " marks");
    } else {
        std::vector<size_t> renamed, moved, turned;
        for (size_t i = 0; i < after; ++i) {
            const SpikeMark &m = a.marks[i], &n = b.marks[i];
            if (m.name != n.name) renamed.push_back(i);
            if (m.x != n.x || m.z != n.z)
                moved.push_back(i);
            else if (m.yaw != n.yaw)
                turned.push_back(i);
        }
        if (renamed.size() == 1)
            parts->push_back("Rename mark " + a.marks[renamed[0]].name + " to " + b.marks[renamed[0]].name);
        else if (!renamed.empty())
            parts->push_back("Rename marks");
        if (moved.size() == 1)
            parts->push_back("Move mark " + b.marks[moved[0]].name);
        else if (!moved.empty())
            parts->push_back("Move " + std::to_string(moved.size()) + " marks");
        if (turned.size() == 1)
            parts->push_back("Turn mark " + b.marks[turned[0]].name);
        else if (!turned.empty())
            parts->push_back("Turn " + std::to_string(turned.size()) + " marks");
    }
    // Walk links changed: (linking deletes a juggler's own keyframes, unlinking writes them out:
    // not changes of their own).
    std::vector<bool> linkChanged(std::max(a.links.size(), b.links.size()), false);
    for (size_t j = 0; j < linkChanged.size(); ++j)
        linkChanged[j] = !sameLink(a.link(static_cast<int>(j)), b.link(static_cast<int>(j)));
    // Keyframes (not when marks were deleted: their keyframes go with them, and the rest are
    // renumbered).
    if (after >= before) {
        std::vector<int> changed;
        for (size_t j = 0; j < std::max(a.keys.size(), b.keys.size()); ++j) {
            if (j < linkChanged.size() && linkChanged[j]) continue;
            const std::vector<Keyframe>&ka = keysOf(a, j), &kb = keysOf(b, j);
            bool same = ka.size() == kb.size();
            for (size_t i = 0; same && i < ka.size(); ++i) same = sameKeyframe(ka[i], kb[i]);
            if (!same) changed.push_back(static_cast<int>(j));
        }
        if (changed.size() == 1) {
            const size_t j = static_cast<size_t>(changed[0]);
            const std::vector<Keyframe>&ka = keysOf(a, j), &kb = keysOf(b, j);
            const std::string who = "J" + std::to_string(j + 1);
            // One keyframe more or fewer: on which beat.
            int beat = -1;
            if (kb.size() == ka.size() + 1 || ka.size() == kb.size() + 1) {
                const std::vector<Keyframe>& more = kb.size() > ka.size() ? kb : ka;
                const std::vector<Keyframe>& fewer = kb.size() > ka.size() ? ka : kb;
                for (const Keyframe& k : more) {
                    bool found = false;
                    for (const Keyframe& f : fewer) found = found || f.beat == k.beat;
                    if (!found) beat = k.beat;
                }
            }
            if (beat >= 0)
                parts->push_back(who + (kb.size() > ka.size() ? ": keyframe on beat " : ": delete keyframe on beat ") +
                                 std::to_string(beat + 1));
            else
                parts->push_back(who + "'s keyframes");
        } else if (!changed.empty()) {
            parts->push_back("Keyframes for " + jugglerList(changed));
        }
    }
    // Walk links.
    for (size_t j = 0; j < linkChanged.size(); ++j) {
        if (!linkChanged[j]) continue;
        const WalkLink la = a.link(static_cast<int>(j)), lb = b.link(static_cast<int>(j));
        const std::string who = "J" + std::to_string(j + 1);
        if (!lb.active())
            parts->push_back("Unlink " + who);
        else if (!la.active() || la.leader != lb.leader)
            parts->push_back(who + " walks like J" + std::to_string(lb.leader + 1));
        else
            parts->push_back(who + "'s walk link");
    }
    if (a.cycle != b.cycle) {
        auto length = [](int cycle) { return cycle > 0 ? std::to_string(cycle) : std::string("the period"); };
        parts->push_back("Choreography length " + length(a.cycle) + " to " + length(b.cycle));
    }
    bool sameLoop = a.repeat == b.repeat;
    for (size_t j = 0; sameLoop && j < std::max(a.next.size(), b.next.size()); ++j) {
        const Continuation ca = a.continuation(static_cast<int>(j)), cb = b.continuation(static_cast<int>(j));
        sameLoop = ca.role == cb.role && ca.turn == cb.turn;
    }
    if (!sameLoop) parts->push_back("Walking loop");
    if (a.grid != b.grid) parts->push_back("Floor grid");
    if (a.groups != b.groups) parts->push_back("Mark groups");
    if (parts->empty()) parts->push_back("Choreography");
}

}  // namespace

std::string describeChange(const Document& a, const Document& b) {
    std::vector<std::string> parts;
    if (a.siteswap != b.siteswap) {
        constexpr size_t kShort = 16;  // (longer ones aren't worth spelling out)
        parts.push_back(a.siteswap.size() <= kShort && b.siteswap.size() <= kShort && !a.siteswap.empty()
                            ? "Throws " + a.siteswap + " to " + b.siteswap
                            : std::string("Throws"));
    }
    if (a.count != b.count) parts.push_back(b.count >= 2 ? std::to_string(b.count) + "-count" : std::string("No count"));
    if (a.throwNext != b.throwNext) parts.push_back("Throwing loop");
    if (a.choreography != b.choreography) describeChoreographyChange(a.choreography, b.choreography, &parts);
    if (a.prop != b.prop) parts.push_back(std::string("Props: ") + propTypeName(b.prop));
    if (a.tempo != b.tempo) parts.push_back(formatted("Tempo %.0f to %.0f", a.tempo, b.tempo));
    if (a.dwell != b.dwell) parts.push_back(formatted("Dwell %.2f to %.2f", a.dwell, b.dwell));
    if (a.distance != b.distance) {
        auto distance = [](double d) { return d > 0.0 ? formatted("%.2f m", d, 0.0) : std::string("auto"); };
        parts.push_back("Distance " + distance(a.distance) + " to " + distance(b.distance));
    }
    if (parts.empty()) return "Change";
    std::string label = parts[0];
    for (size_t i = 1; i < parts.size() && i < 3; ++i) label += ", " + parts[i];
    if (parts.size() > 3) label += ", ...";
    return label;
}

void startHistory(UndoHistory& h, const Document& now) {
    h.undo.clear();
    h.redo.clear();
    h.recorded = now;
}

bool recordStep(UndoHistory& h, const Document& now, const std::string& label) {
    if (now.sameForUndo(h.recorded)) return false;
    UndoStep step;
    step.before = h.recorded;
    step.after = now;
    step.label = label.empty() ? describeChange(h.recorded, now) : label;
    h.undo.push_back(step);
    if (h.undo.size() > UndoHistory::kMaxSteps) h.undo.erase(h.undo.begin());
    h.redo.clear();
    h.recorded = now;
    return true;
}

namespace {

// The camera isn't undone: the document to show keeps the one there is now.
Document withCamera(const Document& d, const Document& now) {
    Document shown = d;
    shown.hasCamera = now.hasCamera;
    for (int i = 0; i < 5; ++i) shown.camera[i] = now.camera[i];
    return shown;
}

}  // namespace

bool undoStep(UndoHistory& h, const Document& now, Document* show, std::string* label) {
    if (h.undo.empty()) return false;
    const UndoStep step = h.undo.back();
    h.undo.pop_back();
    h.redo.push_back(step);
    *show = withCamera(step.before, now);
    *label = step.label;
    h.recorded = *show;
    return true;
}

bool redoStep(UndoHistory& h, const Document& now, Document* show, std::string* label) {
    if (h.redo.empty()) return false;
    const UndoStep step = h.redo.back();
    h.redo.pop_back();
    h.undo.push_back(step);
    *show = withCamera(step.after, now);
    *label = step.label;
    h.recorded = *show;
    return true;
}
