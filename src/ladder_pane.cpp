// ladder_pane.cpp - see ladder_pane.h.

#include "ladder_pane.h"

#include "stage_overlays.h"

#include <algorithm>
#include <string>
#include <vector>

LadderPaneRequest drawLadderPane(LadderPane& pane, const DocumentState& doc, const StageState& stage,
                                 const StageView& stageView, int hoverThrow, const Playback& playback,
                                 const AppSettings& settings, const JugglingLoop& written, const JugglingLoop& shown,
                                 ImVec2 pos, ImVec2 size, ImGuiWindowFlags flags) {
    LadderPaneRequest r;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    // The ladder handles the mouse wheel itself (pan/zoom), so the pane mustn't scroll.
    pane.ladderPreview = false;
    if (ImGui::Begin("Ladder", nullptr, flags | ImGuiWindowFlags_NoScrollWithMouse)) {
        // What the ladder shows and edits: the sketch, or the pattern (as written).
        r.toolbar =
            drawLadderToolbar(written, pane.ladderEdit, settings.showThrowValues, settings.colorByOrbit,
                              doc.parsed.form.hasLinks(), settings.dimLinked);
        // (The view's own changes are made here; the rest by the caller.)
        if (r.toolbar.toggleCollapseAll) toggleCollapseAll(pane.ladderEdit, written.jugglers);
        if (r.toolbar.resetView) resetLadderView(pane.ladderEdit);
        LadderViewOptions ladderOptions;
        ladderOptions.showValues = settings.showThrowValues;
        ladderOptions.colorByOrbit = settings.colorByOrbit;
        ladderOptions.selectedJuggler = stageView.selectedJuggler;
        ladderOptions.highlightThrow = hoverThrow;
        ladderOptions.relativeTargets = doc.parsed.form.relative;
        ladderOptions.form = &doc.parsed.form;
        ladderOptions.countN = doc.patternCount;
        ladderOptions.writtenPeriod = stage.derived.played.expanded ? stage.derived.played.period : 0;
        ladderOptions.dimLinked = settings.dimLinked;
        ladderOptions.followPlayhead = playback.playing;
        {
            const JugglingLoop spinLoop = doc.patternValid && !doc.sketching ? stage.derived.played.loop : JugglingLoop();
            if (spinLoop != pane.spinCacheLoop || doc.juggleParams.bpm != pane.spinCacheParams.bpm ||
                doc.juggleParams.dwellBeats != pane.spinCacheParams.dwellBeats ||
                doc.juggleParams.prop != pane.spinCacheParams.prop ||
                doc.juggleParams.distance != pane.spinCacheParams.distance) {
                pane.spinCache = throwSpins(spinLoop, doc.juggleParams);
                pane.spinCacheLoop = spinLoop;
                pane.spinCacheParams = doc.juggleParams;
            }
            ladderOptions.spins = &pane.spinCache;
        }
        {
            const JugglingLoop& ladderLoop = shown;
            const int written = choreographyCycle(ladderLoop, doc.juggleParams);  // (the beats written out)
            ladderOptions.keyframeCycle = choreographyFullCycle(doc.choreography, written);
            ladderOptions.showKeyframeMarks = settings.showKeyframeMarks;
            ladderOptions.walkLinks = stageView.choreographyMode && ladderLoop.jugglers > 1;
            const int cycle = ladderOptions.keyframeCycle;
            ladderOptions.walkLabels.assign(static_cast<size_t>(std::max(0, ladderLoop.jugglers)), std::string());
            for (int j = 0; j < ladderLoop.jugglers; ++j)
                ladderOptions.walkLabels[static_cast<size_t>(j)] = walkLinkLabel(doc.choreography.link(j));
            for (size_t j = 0; j < static_cast<size_t>(std::max(0, ladderLoop.jugglers)); ++j) {
                const std::vector<Keyframe> keys =
                    effectiveKeyframes(doc.choreography, static_cast<int>(j), written);
                const bool linked = doc.choreography.link(static_cast<int>(j)).active();
                for (size_t i = 0; i < keys.size(); ++i) {
                    const Keyframe& k = keys[i];
                    LadderKeyframe lk;
                    lk.juggler = static_cast<int>(j);
                    lk.beat = k.beat;
                    const bool onMark = k.mark >= 0 && k.mark < static_cast<int>(doc.choreography.marks.size());
                    lk.where = onMark ? "on spike mark " + doc.choreography.marks[static_cast<size_t>(k.mark)].name
                                      : "a turned spot";
                    lk.longTurn = k.longTurn;
                    lk.mark = onMark ? k.mark : -1;
                    if (onMark) lk.markName = doc.choreography.marks[static_cast<size_t>(k.mark)].name;
                    lk.derived = linked || k.beat >= written;  // (a link's, or a rotated repeat's: locked)
                    if (cycle > 0) {
                        const Keyframe& next = keys[(i + 1) % keys.size()];
                        const int gap = keys.size() == 1 ? cycle : ((next.beat - k.beat) % cycle + cycle) % cycle;
                        if (gap > 0 && sameKeyframeSpot(doc.choreography, k, next)) lk.stayBeats = gap;
                    }
                    ladderOptions.keyframes.push_back(lk);
                }
            }
        }
        // (It shows the loop played out: what happens on each beat.)
        r.edited =
            drawLadderDiagram(stage.derived.played.expanded ? stage.derived.played.loop : written, settings.colorVision,
                              pane.ladderEdit, playback.beat, ladderOptions);
        pane.ladderPreview = r.edited.hasPreview;
        if (pane.ladderPreview) pane.ladderPreviewLoop = r.edited.preview;
    }
    ImGui::End();
    return r;
}
