// document.cpp - see document.h.

#include "document.h"

#include "pattern.h"

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
