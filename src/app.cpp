// app.cpp - JuggleSim itself (see app.h): the panes, editing, playback and drawing.
//
// Layout (below the main menu bar):
//   +----------------------+----------------------+
//   |                      |                      |
//   |    ladder diagram    |   3D juggler view    |
//   |                      |  (plain GL viewport) |
//   +----------------------+                      |
//   |  siteswap entry      |                      |
//   +----------------------+----------------------+
// The divider between the two halves can be dragged.

#include "app.h"

#include "app_widgets.h"
#include "app_state.h"
#include "camera.h"
#include "panels.h"
#include "stage_overlays.h"
#include "stage_render.h"

#include "color_vision.h"
#include "gl_funcs.h"
#include "juggle_sim.h"
#include "juggler_figure.h"
#include "ladder_view.h"
#include "loop_ops.h"
#include "math3d.h"
#include "mesh.h"
#include "pattern.h"
#include "pattern_library.h"
#include "pattern_library_ui.h"
#include "platform.h"
#include "prop_figure.h"
#include "renderer.h"
#include "settings.h"
#include "choreography.h"
#include "siteswap.h"
#include "siteswap_edit.h"
#include "siteswap_grid.h"
#include "sound.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

int runApp() {
    App app;
    Graphics& gfx = app.gfx;
    DocumentState& doc = app.doc;
    StageState& stage = app.stage;
    StageView& stageView = app.stageView;
    LadderPane& ladderPane = app.ladderPane;
    GridPane& gridPane = app.gridPane;
    LibraryState& library = app.library;
    UiState& ui = app.ui;
    SoundState& sound = app.sound;
    AppSettings& settings = app.settings;
    Playback& playback = app.playback;

    std::string missing;
    if (!gl::load(&missing)) {
        const std::string msg = "Missing OpenGL functions (driver too old?):\n" + missing;
        platformShowError("JuggleSim", msg.c_str());
        return 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout; nothing worth persisting yet
    // Keyboard navigation between widgets is left off: Space and the arrow keys drive playback.
    ImGui::StyleColorsDark();
    // Fonts: the classic pixel font for the UI (the first one added is the default), and the
    // scalable default font for larger text (the pattern's name over the juggler pane).
    io.Fonts->AddFontDefaultBitmap();
    gfx.titleFont = io.Fonts->AddFontDefaultVector();
    constexpr float kTitleFontSize = 20.0f;

    ImGui_ImplOpenGL3_Init();  // (picks the right GLSL version for the platform)

    std::string error;
    if (!gfx.renderer.init(&error)) {
        platformShowError("JuggleSim: shader error", error.c_str());
        ImGui_ImplOpenGL3_Shutdown();
        return 1;
    }
    gfx.prims.create();
    gfx.propMeshes.create();

    settings = loadSettings();

    doc.parsed = parseSiteswap(doc.siteswapText);
    doc.pattern = patternFromSiteswap(doc.parsed);
    doc.patternValid = doc.parsed.valid;
    doc.savedPatternText = doc.siteswapText;
    doc.history.current = doc.siteswapText;

    doc.juggleParams.prop = settings.prop;
    doc.juggleParams.choreography = &doc.choreography;  // (the stage's evaluation uses stageParams, below)
    auto choreographyText = [&]() {
        return doc.choreography.active() ? choreographyToText(doc.choreography) : std::string();
    };
    auto markSaved = [&]() {
        doc.savedPatternText = doc.siteswapText;
        doc.savedChoreography = choreographyText();
        doc.savedThrowNext = throwNextText(doc.throwNext);
    };
                                    // (not for selecting a juggler standing on it)
    // The loop the jugglers juggle (the sketch, or the pattern; empty if neither).
    auto shownLoop = [&]() -> JugglingLoop {
        if (doc.sketching) return doc.sketchLoop;
        if (doc.patternValid) return patternLoop(doc.pattern);
        // (The throws have an error: see lastGoodLoop.)
        if (!stage.lastGoodLoop.empty() && stage.lastGoodLoop.jugglers == doc.parsed.loop.jugglers)
            return stage.lastGoodLoop;
        return JugglingLoop();
    };
    // Spike marks are kept in meters for the distance they were placed at, and scale with it.
    // A spike mark's name ("0a"), or "?" for none.
    auto markNameOf = [&](int m) {
        return m >= 0 && m < static_cast<int>(doc.choreography.marks.size())
                   ? doc.choreography.marks[static_cast<size_t>(m)].name
                   : std::string("?");
    };
    auto choreographyScale = [&](const JugglingLoop& loop) {
        if (doc.choreography.reference <= 0.0 || loop.empty()) return 1.0f;
        return static_cast<float>(passingDistance(loop, doc.juggleParams) / doc.choreography.reference);
    };
    // Starts a choreography if there isn't one: a spike mark where each juggler stands now
    // (the default formation), facing as they do.
    auto ensureSpikeMarks = [&]() {
        if (doc.choreography.active()) return;
        const JugglingLoop loop = shownLoop();
        if (loop.empty()) return;
        Choreography c;
        c.reference = passingDistance(loop, doc.juggleParams);
        c.cycle = loop.period;  // its own length from now on (editing the siteswap doesn't change it)
        c.grid = floorGridText(settings);  // (made on the grid in use)
        doc.choreographyGridShown = c.grid;
        for (int j = 0; j < loop.jugglers; ++j) {
            Vec3 position;
            float yaw = 0.0f;
            jugglerPlacement(loop, doc.juggleParams, j, &position, &yaw);
            SpikeMark m;
            m.x = position.x;
            m.z = position.z;
            m.yaw = yaw;
            addSpikeMark(&c, m);
        }
        doc.choreography = c;
        doc.untouchedChoreography = choreographyToText(doc.choreography);
    };
    // What of the groups the floor shows this frame: all of them (badges and lines) while Ctrl is
    // held; else the lines of the group under the mouse in the panel, or of the one selected.
    auto markGroupView = [&]() {
        MarkGroupView v;
        if (!stageView.choreographyMode || !doc.choreography.active()) return v;
        if (ImGui::GetIO().KeyCtrl) {
            v.slots = doc.choreography.groupsUsed();
            for (int g = 0; g < v.slots; ++g)
                if (!doc.choreography.group(g).empty()) v.lines.push_back(g);
        } else if (stageView.groupChipHovered >= 0) {
            v.lines.push_back(stageView.groupChipHovered);
        } else if (stageView.selectedGroup >= 0 && !doc.choreography.group(stageView.selectedGroup).empty()) {
            v.lines.push_back(stageView.selectedGroup);
        }
        return v;
    };
    auto markListText = [&](const std::vector<int>& marks) {
        std::string list;
        for (size_t i = 0; i < marks.size(); ++i)
            list += (i == 0 ? "" : (i + 1 == marks.size() ? " and " : ", ")) +
                    (marks[i] >= 0 && marks[i] < static_cast<int>(doc.choreography.marks.size())
                         ? doc.choreography.marks[static_cast<size_t>(marks[i])].name
                         : std::string("?"));
        return list;
    };
    // Makes group g (0-based) these marks (none: it's emptied). An undo step like any change.
    auto setMarkGroup = [&](int g, std::vector<int> marks) {
        if (g < 0 || g >= Choreography::kMarkGroups) return;
        std::sort(marks.begin(), marks.end());
        marks.erase(std::unique(marks.begin(), marks.end()), marks.end());
        if (static_cast<int>(doc.choreography.groups.size()) <= g)
            doc.choreography.groups.resize(static_cast<size_t>(g) + 1);
        doc.choreography.groups[static_cast<size_t>(g)] = marks;
        while (!doc.choreography.groups.empty() && doc.choreography.groups.back().empty())
            doc.choreography.groups.pop_back();
        const char key = markGroupKey(g);
        gridPane.siteswapBox.info = marks.empty() ? std::string("Group ") + key + " emptied."
                                         : std::string("Group ") + key + ": " + (marks.size() == 1 ? "mark " : "marks ") +
                                               markListText(marks) + ". Press " + key + " to select them again, Shift+" + key +
                                               " to add them to a selection; hold Ctrl to see the groups.";
        gridPane.siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
        stageView.selectedGroup = marks.empty() ? -1 : g;
    };
    // The spike marks whose rings contain a floor point (world).
    auto marksAround = [&](float x, float z, float scale) {
        std::vector<int> found;
        for (int m = 0; m < static_cast<int>(doc.choreography.marks.size()); ++m) {
            const SpikeMark& sm = doc.choreography.marks[static_cast<size_t>(m)];
            if (std::hypot(x - sm.x * scale, z - sm.z * scale) <= kMarkRadius * scale) found.push_back(m);
        }
        return found;
    };
    auto openLoiterPopup = [&](int juggler, const Keyframe& arrival, ImVec2 at) {
        ui.loiter.openRequested = true;
        ui.loiter.juggler = juggler;
        ui.loiter.arrival = arrival;
        ui.loiter.onBeat = std::lround(playback.beat);
        ui.loiter.at = at;
        ui.loiter.text[0] = '\0';
        ui.loiter.error.clear();
        ui.loiter.errorUntil = 0.0;
    };
    auto openWalkLike = [&](int juggler) {
        ensureSpikeMarks();
        ui.walkLike.open = true;
        ui.walkLike.juggler = juggler;
        ui.walkLike.confirmLeader = -1;
    };
    // Unlinks a juggler who walks like another: their walking written out as their own keyframes,
    // so nothing moves (only the beats written out: the rotated repeats follow from them), on the
    // marks there, or new marks where the turn puts them somewhere there isn't one.
    auto unlinkWalk = [&](int j) {
        const JugglingLoop loop = shownLoop();
        const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
        if (cycle <= 0 || !doc.choreography.link(j).active()) return;
        std::vector<Keyframe> baked = effectiveKeyframes(doc.choreography, j, cycle);
        baked.erase(std::remove_if(baked.begin(), baked.end(), [&](const Keyframe& k) { return k.beat >= cycle; }), baked.end());
        for (Keyframe& k : baked)
            if (k.mark < 0) k.mark = markForSpot(&doc.choreography, k.x, k.z, k.yaw);
        setWalkLink(&doc.choreography, j, WalkLink());
        if (static_cast<int>(doc.choreography.keys.size()) <= j)
            doc.choreography.keys.resize(static_cast<size_t>(j) + 1);
        doc.choreography.keys[static_cast<size_t>(j)] = baked;
    };
    // The choreography's rows for the siteswap grid (see ChoreoGridView): per juggler, the mark
    // they arrive on each beat, loiters, and copies (walk links, rotated repeats) dim.
    auto choreoGridView = [&]() {
        ChoreoGridView view;
        const JugglingLoop loop = shownLoop();
        if (!doc.choreography.active() || loop.empty()) return view;
        const int written = choreographyCycle(loop, doc.juggleParams);
        const int n = loop.jugglers;
        const WalkLoop w = walkLoop(doc.choreography, n);
        const int full = written * w.phases;
        if (written <= 0 || full <= 0) return view;
        view.active = true;
        view.written = written;
        view.full = full;
        view.loopProblem = w.valid ? std::string() : w.problem;
        for (const SpikeMark& m : doc.choreography.marks) view.markNames.push_back(m.name);
        view.rows.resize(static_cast<size_t>(n));
        // A juggler's own keyframes in the beats written out (and their loiters).
        auto ownCells = [&](int j) {
            std::vector<ChoreoGridCell> cells(static_cast<size_t>(written));
            if (!doc.choreography.hasKeys(j)) return cells;
            const std::vector<Keyframe>& keys = doc.choreography.keys[static_cast<size_t>(j)];
            for (const Keyframe& k : keys) {
                if (k.beat < 0 || k.beat >= written) continue;
                ChoreoGridCell& cell = cells[static_cast<size_t>(k.beat)];
                cell.text = k.mark >= 0 && k.mark < static_cast<int>(doc.choreography.marks.size())
                                ? doc.choreography.marks[static_cast<size_t>(k.mark)].name
                                : std::string("?");
                if (k.longTurn) cell.text += "~";
            }
            // Loiters: between two keyframes on the same spot (round the end only when the loop
            // brings them back to their own part, unturned).
            const bool wraps = doc.choreography.continuation(j).isDefault(j);
            for (size_t i = 0; i < keys.size(); ++i) {
                const bool last = i + 1 == keys.size();
                if (last && !wraps) break;
                const Keyframe& a = keys[i];
                const Keyframe& b = keys[last ? 0 : i + 1];
                if (keys.size() < 2 || !sameKeyframeSpot(doc.choreography, a, b)) continue;
                const int end = last ? b.beat + written : b.beat;
                for (int beat = a.beat + 1; beat < end; ++beat) cells[static_cast<size_t>(beat % written)].stay = true;
            }
            return cells;
        };
        // Each row over the whole loop: its own path, then (dim) the same again each time round (the
        // rows are positions: who's on one is in its name); or, with a walk link, the leader's,
        // offset (dim; the turn is in the header).
        std::vector<char> state(static_cast<size_t>(n), 0);  // 0 not yet, 1 working on, 2 done
        std::function<const std::vector<ChoreoGridCell>&(int)> rowOf = [&](int j) -> const std::vector<ChoreoGridCell>& {
            ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
            if (state[static_cast<size_t>(j)] != 0) return row.cells;  // (done, or round in a circle)
            state[static_cast<size_t>(j)] = 1;
            row.cells.assign(static_cast<size_t>(full), ChoreoGridCell());
            const WalkLink l = doc.choreography.link(j);
            if (l.active() && l.leader >= 0 && l.leader < n && !doc.choreography.dependsOn(l.leader, j)) {
                const std::vector<ChoreoGridCell> lead = rowOf(l.leader);
                for (int beat = 0; beat < full && !lead.empty(); ++beat) {
                    row.cells[static_cast<size_t>(beat)] = lead[static_cast<size_t>(((beat + l.offset) % full + full) % full)];
                    row.cells[static_cast<size_t>(beat)].locked = true;
                }
                row.link = "@" + std::to_string(l.leader + 1) + "[" + std::to_string(l.offset) + "]" +
                           (l.turn.none() ? std::string() : " " + turnShort(l.turn));
            } else {
                const std::vector<ChoreoGridCell> part = ownCells(j);
                for (int p = 0; p < w.phases; ++p) {
                    for (int beat = 0; beat < written && beat < static_cast<int>(part.size()); ++beat) {
                        ChoreoGridCell cell = part[static_cast<size_t>(beat)];
                        cell.locked = p > 0 || cell.locked;
                        row.cells[static_cast<size_t>(p * written + beat)] = cell;
                    }
                }
            }
            state[static_cast<size_t>(j)] = 2;
            return row.cells;
        };
        for (int j = 0; j < n; ++j) {
            rowOf(j);
            ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
            row.loop = continuationText(doc.choreography.continuation(j), j);
            row.loopBad = std::find(w.bad.begin(), w.bad.end(), j) != w.bad.end();
        }
        return view;
    };
    // (On means showing: the mode can be left on from a pattern with a choreography when one
    // without is loaded, and then the first press should turn it on, not off.)
    // Sets a juggler's loop column (see Continuation). An older file's one turn for everyone is
    // written out per juggler first. A loop column that doesn't work (a part taken twice, or by
    // nobody) is kept, and says so.
    auto setContinuation = [&](int j, const Continuation& k) {
        const JugglingLoop loop = shownLoop();
        const int n = std::max(loop.empty() ? 0 : loop.jugglers, j + 1);
        if (static_cast<int>(doc.choreography.next.size()) < n || !doc.choreography.repeat.none()) {
            std::vector<Continuation> all(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) all[static_cast<size_t>(i)] = doc.choreography.continuation(i);
            doc.choreography.next = all;
            doc.choreography.repeat = Turn();
        }
        if (doc.choreography.cycle == 0 && !loop.empty())
            doc.choreography.cycle = choreographyCycle(loop, doc.juggleParams);
        doc.choreography.next[static_cast<size_t>(j)] = k;
        const WalkLoop w = walkLoop(doc.choreography, n);
        if (!w.valid) {
            gridPane.siteswapBox.note = "The walking loop doesn't work: " + w.problem + ".";
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 8.0;
        }
    };
    // A beat of everyone's walking inserted before beat `beat` (or deleted): the keyframes after it
    // move along, and the Length with them.
    auto walkingBeat = [&](int beat, bool insert) {
        const JugglingLoop loop = shownLoop();
        if (loop.empty()) return;
        const int written = choreographyCycle(loop, doc.juggleParams);
        if (!insert && written <= 1) return;
        for (std::vector<Keyframe>& keys : doc.choreography.keys) {
            if (!insert)
                keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Keyframe& k) { return k.beat == beat; }), keys.end());
            for (Keyframe& k : keys)
                if (k.beat >= beat + (insert ? 0 : 1)) k.beat += insert ? 1 : -1;
        }
        doc.choreography.cycle = std::clamp(written + (insert ? 1 : -1), 1, kMaxChoreographyLength);
    };
    auto toggleChoreographyMode = [&]() {
        stageView.choreographyMode = !(stageView.choreographyMode && doc.choreography.active());
        if (stageView.choreographyMode) ensureSpikeMarks();
        stageView.markDrag = -1;
    };

    // The settings a library pattern can carry, as they are now (all of them).
    auto currentPatternSettings = [&]() -> PatternSettings {
        PatternSettings current;
        current.hasProp = current.hasTempo = current.hasDwell = current.hasDistance = current.hasChoreography = true;
        current.hasCount = true;
        current.count = doc.patternCount;
        current.hasThrowNext = true;
        current.throwNext = throwNextText(doc.throwNext);
        current.choreography = doc.choreography.active() ? choreographyToText(doc.choreography) : std::string();
        current.prop = doc.juggleParams.prop;
        current.tempo = doc.juggleParams.bpm;
        current.dwell = doc.juggleParams.dwellBeats;
        current.distance = doc.juggleParams.distance;
        return current;
    };
    // ...and for saving: with the camera too, if the user has placed it.
    auto settingsForSaving = [&]() -> PatternSettings {
        PatternSettings s = currentPatternSettings();
        if (stageView.camera.free) {
            s.hasCamera = true;
            s.camera[0] = stageView.camera.freePos.x;
            s.camera[1] = stageView.camera.freePos.y;
            s.camera[2] = stageView.camera.freePos.z;
            s.camera[3] = stageView.camera.freeYaw * 180.0 / kPi;
            s.camera[4] = stageView.camera.freePitch * 180.0 / kPi;
        }
        return s;
    };
    // Applies the settings a library pattern (or an undo step) carries; leaves the rest alone.
    auto applyPatternSettings = [&](const PatternSettings& apply) {
        if (apply.hasProp && apply.prop != settings.prop) {
            settings.prop = apply.prop;  // the Props menu choice is remembered between runs
            saveSettings(settings);
        }
        doc.juggleParams.prop = settings.prop;
        if (apply.hasTempo) doc.juggleParams.bpm = apply.tempo;
        if (apply.hasDwell) doc.juggleParams.dwellBeats = apply.dwell;
        if (apply.hasDistance) doc.juggleParams.distance = apply.distance;
        if (apply.hasCount) doc.patternCount = apply.count;
        if (apply.hasThrowNext && !readThrowNext(apply.throwNext, &doc.throwNext)) doc.throwNext.clear();
        if (apply.hasCamera) {
            stageView.camera.free = true;
            stageView.camera.topView = false;
            stageView.camera.topBlend = 0.0f;
            stageView.camera.freePos = Vec3(static_cast<float>(apply.camera[0]), static_cast<float>(apply.camera[1]),
                                  static_cast<float>(apply.camera[2]));
            stageView.camera.freeYaw = static_cast<float>(apply.camera[3] * kPi / 180.0);
            stageView.camera.freePitch =
                std::clamp(static_cast<float>(apply.camera[4] * kPi / 180.0), -kMaxFreePitch, kMaxFreePitch);
        }
        if (apply.hasChoreography) {
            Choreography c;
            if (apply.choreography.empty() || !choreographyFromText(apply.choreography, &c)) c = Choreography();
            doc.choreography = c;
            stageView.selectedMarks.clear();
            // The jugglers stand where it says, in meters at the distance it was made for.
            if (c.active() && c.reference > 0.0) doc.juggleParams.distance = c.reference;
            stageView.markDrag = -1;
        }
    };

    // Settings changes become undo steps by polling: once the settings differ from the last
    // recorded ones and nothing has changed (and no control is held) for kSettingsSettleSeconds,
    // the change is recorded. Anything else that records a step records a pending settings
    // change first, so the steps stay in order.
    constexpr double kSettingsSettleSeconds = 0.6;
    doc.recordedSettings = currentPatternSettings();
    doc.lastSeenSettings = doc.recordedSettings;
    auto recordSettingsChange = [&]() {
        const PatternSettings now = currentPatternSettings();
        if (now != doc.recordedSettings) doc.history.recordWithSettings(doc.history.current, doc.recordedSettings, now);
        doc.recordedSettings = doc.lastSeenSettings = now;
    };
    auto pollSettingsChange = [&]() {
        const PatternSettings now = currentPatternSettings();
        if (now != doc.lastSeenSettings) {
            doc.lastSeenSettings = now;
            doc.settingsChangedAt = ImGui::GetTime();
        }
        // (Held while the loiter popup is open: the arrival and the loiter are one undo step.)
        if (ImGui::IsAnyItemActive() || ui.loiter.open || ui.loiter.openRequested)
            doc.settingsChangedAt = ImGui::GetTime();
        if (now != doc.recordedSettings && ImGui::GetTime() - doc.settingsChangedAt >= kSettingsSettleSeconds)
            recordSettingsChange();
    };
    // After undo/redo or a library load: what's showing is what's recorded.
    auto settingsRecorded = [&]() { doc.recordedSettings = doc.lastSeenSettings = currentPatternSettings(); };
    auto recordPattern = [&](const std::string& text) {
        recordSettingsChange();
        doc.history.record(text);
    };
    // A throw being typed into the siteswap grid is shown as it's typed, and becomes an undo step
    // once it's finished; but a change made some other way (the ladder, undo, the library) can
    // come first. Each of those calls this, so what was typed is a step of its own and undoing the
    // change goes back to it.
    auto commitTyping = [&]() {
        if (doc.history.current != doc.siteswapText) recordPattern(doc.siteswapText);
        gridPane.siteswapGrid.editing = false;
    };

    // Replaces the pattern with the one written in `text` (used by undo/redo).
    auto showPatternText = [&](const std::string& text) {
        cancelLadderEdit(ladderPane.ladderEdit);
        std::snprintf(doc.siteswapText, sizeof(doc.siteswapText), "%s", text.c_str());
        doc.parsed = parseSiteswap(doc.siteswapText);
        doc.patternValid = doc.parsed.valid;
        doc.sketching = doc.parsed.sketch;
        if (doc.parsed.valid) doc.pattern = patternFromSiteswap(doc.parsed);
        if (doc.parsed.sketch) doc.sketchLoop = doc.parsed.loop;
    };
    // Makes a loop from the ladder the pattern (an edit, a throw drawn or deleted, beats added or
    // deleted): a complete one written at its shortest period, a sketch as it is. One undo step.
    // With linked jugglers, the change goes to every one of them (whichever was edited), or is
    // refused with the reason shown under the siteswap box.
    auto applyLoop = [&](const JugglingLoop& changed) {
        commitTyping();
        JugglingLoop l = changed;
        std::string why;
        if (!projectEdit(doc.parsed.form, doc.parsed.loop, changed, &l, &why)) {
            gridPane.siteswapBox.note = "Can't: " + why;
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return;
        }
        GridCount gridCount;
        gridCount.n = doc.patternCount;
        if (openThrowCount(l) == 0) l = loopWithPeriod(l, countPeriodFor(loopShortestPeriod(l), gridCount));
        std::string text;
        // Written the way the pattern was typed: targets as they were, links as links.
        if (!loopToText(l, &text, &doc.parsed.form)) return;
        // A count directive: the 3s between its beats stay as they are.
        SiteswapGrid gridBefore, gridAfter;
        if (gridCount.active() && gridFromText(doc.siteswapText, &gridBefore) && gridFromText(text, &gridAfter) &&
            !countAllowsChange(gridBefore, gridAfter, gridCount, &why)) {
            gridPane.siteswapBox.note = "Can't: " + why + ".";
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return;
        }
        const Siteswap check = parseSiteswap(text);
        if (!check.valid && !check.sketch) return;  // shouldn't happen; keep what we have
        std::snprintf(doc.siteswapText, sizeof(doc.siteswapText), "%s", text.c_str());
        doc.parsed = check;
        doc.patternValid = check.valid;
        doc.sketching = check.sketch;
        if (check.valid) doc.pattern = patternFromSiteswap(check);
        if (check.sketch) doc.sketchLoop = check.loop;
        recordPattern(text);
    };
    // Makes juggler j a copy of juggler `to` from to's beat `start` on ("Same as", from the ladder
    // or the grid), or writes a linked juggler's throws out again. One undo step.
    auto relink = [&](int j, int to, int start, bool unlink) {
        PatternForm newForm;
        JugglingLoop newLoop = doc.parsed.loop;
        std::string why, text;
        bool ok = true;
        if (!unlink)
            ok = linkJuggler(doc.parsed.form, doc.parsed.loop, j, to, start, &newForm, &newLoop, &why, true);
        else
            newForm = unlinkJuggler(doc.parsed.form, j);
        if (ok && !doc.parsed.loop.empty() && loopToText(newLoop, &text, &newForm)) {
            commitTyping();
            cancelLadderEdit(ladderPane.ladderEdit);
            showPatternText(text);
            recordPattern(text);
        } else {
            gridPane.siteswapBox.note =
                "Can't: " + (why.empty() ? std::string("the pattern has to be readable first") : why);
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
        }
    };
    auto linkJugglers = [&](int j, int to, int start) { relink(j, to, start, false); };
    auto unlinkJugglers = [&](int j) { relink(j, 0, 0, true); };
    auto undo = [&]() {
        if (ladderPane.ladderEdit.chain.active) {  // an open chain is just cancelled, like Esc
            cancelLadderEdit(ladderPane.ladderEdit);
            return;
        }
        commitTyping();  // a throw being typed is the step to undo
        recordSettingsChange();  // a change still settling is the one to undo
        if (doc.history.undo.empty()) return;
        const HistoryStep step = doc.history.undo.back();
        doc.history.undo.pop_back();
        doc.history.redo.push_back(step);
        doc.history.current = step.before;
        if (step.before != step.after) showPatternText(step.before);
        if (step.settingsChanged) applyPatternSettings(step.settingsBefore);
        settingsRecorded();
    };
    auto redo = [&]() {
        commitTyping();
        recordSettingsChange();  // a change still settling is a new step, which ends redo
        if (doc.history.redo.empty()) return;
        cancelLadderEdit(ladderPane.ladderEdit);
        const HistoryStep step = doc.history.redo.back();
        doc.history.redo.pop_back();
        doc.history.undo.push_back(step);
        doc.history.current = step.after;
        if (step.before != step.after) showPatternText(step.after);
        if (step.settingsChanged) applyPatternSettings(step.settingsAfter);
        settingsRecorded();
    };
    library.builtInPatterns = parsePatternLibrary(platformBuiltInPatternText());
    library.myPatterns = loadUserPatterns();
    library.recentPatterns = loadRecentPatterns();
    for (size_t i = 0; i < library.recentPatterns.size();) {  // My Patterns deleted (e.g. by editing the file) go
        bool onDisk = !library.recentPatterns[i].mine;
        for (const LibraryPattern& saved : library.myPatterns)
            if (saved.displayName() == library.recentPatterns[i].pattern.displayName()) onDisk = true;
        if (onDisk)
            ++i;
        else
            library.recentPatterns.erase(library.recentPatterns.begin() + static_cast<std::ptrdiff_t>(i));
    }

    // Loads a pattern from the library: its siteswap, and whatever settings it carries. One
    // undo step, which also puts the settings back. It goes to the top of Recent.
    auto loadLibraryPattern = [&](const LibraryPattern& chosen, bool mine) {
        commitTyping();
        playback.beat = 0.0;  // a new pattern starts at beat 1
        recordSettingsChange();
        const PatternSettings before = currentPatternSettings();
        showPatternText(chosen.siteswap);
        doc.choreography = Choreography();  // unless the pattern brings its own
        doc.patternCount = 0;               // ...and no count directive, unless it brings one
        doc.throwNext.clear();              // ...and no throwing loop, unless it brings one
        stageView.camera.free = false;            // ...and the automatic framing, unless it brings a camera
        applyPatternSettings(chosen.settings);
        doc.history.recordWithSettings(doc.siteswapText, before, currentPatternSettings());
        settingsRecorded();
        markSaved();
        addRecentPattern(&library.recentPatterns, chosen, mine);
        saveRecentPatterns(library.recentPatterns);  // not worth bothering the user if this fails
        doc.openPatternName = mine ? chosen.displayName() : std::string();
    };

    // Names of known patterns by their siteswap (written at the shortest period, so "51" and a
    // typed "5151" both find the Shower). The user's own names win over the built-in ones.
    auto canonicalSiteswap = [](const Pattern& p) {
        std::string text;
        if (!patternToSiteswap(withPeriod(p, shortestPeriodBeats(p)), &text)) text.clear();
        return text;
    };
    auto rebuildPatternNames = [&]() {
        library.patternNames.clear();
        const std::vector<LibraryPattern>* lists[] = {&library.builtInPatterns, &library.myPatterns};
        for (const std::vector<LibraryPattern>* list : lists) {
            for (const LibraryPattern& p : *list) {
                if (p.name.empty()) continue;
                const Siteswap known = parseSiteswap(p.siteswap);
                if (!known.valid) continue;
                const std::string key = canonicalSiteswap(patternFromSiteswap(known));
                if (!key.empty()) library.patternNames[key] = p.name;
            }
        }
    };
    rebuildPatternNames();
    auto saveMyPatterns = [&]() {
        rebuildPatternNames();
        if (!saveUserPatterns(library.myPatterns))
            ui.fileError = "Couldn't write your patterns to " + userDataPath(L"my_patterns.txt").u8string() +
                        ". The change is kept until JuggleSim closes.";
    };

    auto openPatternIndex = [&]() -> int {
        if (doc.openPatternName.empty()) return -1;
        for (size_t i = 0; i < library.myPatterns.size(); ++i)
            if (library.myPatterns[i].displayName() == doc.openPatternName) return static_cast<int>(i);
        return -1;
    };
    // Saves what's showing to My Patterns as `name`: over the pattern of that name if there is
    // one (replace), else as a new one. It's then the open pattern.
    auto storeMyPattern = [&](const std::string& name, bool replace) {
        LibraryPattern saved;
        saved.siteswap = doc.siteswapText;
        saved.name = name;
        saved.settings = settingsForSaving();
        saved.settings.hasDistance =
            (doc.sketching ? doc.sketchLoop.jugglers : doc.pattern.jugglers) > 1;  // passing only
        classifyPattern(&saved);
        bool replaced = false;
        if (replace) {
            for (LibraryPattern& existing : library.myPatterns) {
                if (existing.displayName() == saved.displayName()) {
                    saved.otherSettings = existing.otherSettings;  // (settings from a newer JuggleSim)
                    existing = saved;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) library.myPatterns.push_back(saved);
        saveMyPatterns();
        markSaved();
        doc.openPatternName = saved.displayName();
        addRecentPattern(&library.recentPatterns, saved, true);  // to the top, replacing any older copy
        saveRecentPatterns(library.recentPatterns);
    };

    // T: the top view (automatically framed), or back to automatic framing.
    auto toggleTopView = [&]() {
        if (stageView.camera.free) {
            stageView.camera.free = false;
            stageView.camera.topView = true;
        } else {
            stageView.camera.topView = !stageView.camera.topView;
        }
    };
    // Selects group g's marks (`add`: adds them to the selection), and no juggler.
    auto selectMarkGroup = [&](int g, bool add) {
        const std::vector<int>& members = doc.choreography.group(g);
        const char key = markGroupKey(g);
        if (members.empty()) {
            gridPane.siteswapBox.info = std::string("Group ") + key +
                                        " is empty: select spike marks (click, Shift+click) and press Ctrl+" + key +
                                        " to make it.";
            gridPane.siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
            return;
        }
        if (!add) stageView.selectedMarks.clear();
        for (const int m : members)
            if (std::find(stageView.selectedMarks.begin(), stageView.selectedMarks.end(), m) ==
                stageView.selectedMarks.end())
                stageView.selectedMarks.push_back(m);
        stageView.selectedJuggler = -1;
        stageView.frameSelected = false;
        stageView.selectedGroup = add ? -1 : g;
    };
    constexpr double kExtentsSettleSeconds = 0.3;

    auto stepPlayback = [&](double beats) {
        playback.playing = false;
        playback.beat += beats;
    };
    // Back to beat 1 (playing or not, as it was).
    // Back to beat 1, with the ladder back to its default view (beat 1 at the top), playing or not.
    auto goToStart = [&]() {
        playback.beat = 0.0;
        resetLadderView(ladderPane.ladderEdit);
        ladderPane.ladderEdit.followPausedUntil = 0.0;
    };

    // Quitting: right away, or first asking whether to save the pattern if it has changed.
    auto requestQuit = [&]() {
        if ((doc.patternValid || doc.sketching) &&
            (doc.savedPatternText != doc.siteswapText || doc.savedChoreography != choreographyText() ||
             doc.savedThrowNext != throwNextText(doc.throwNext))) {
            ui.promptForQuit = true;
            ui.savePromptRequested = true;
        } else {
            ui.done = true;
        }
    };
    while (!ui.done) {
        if (!platformPollEvents()) break;
        if (platformTakeCloseRequest()) ui.closePending = true;
        if (platformIsMinimized()) {
            platformSleepMilliseconds(10);
            continue;
        }
        if (ui.closePending) {
            ui.closePending = false;
            requestQuit();
        }

        ImGui_ImplOpenGL3_NewFrame();
        platformNewFrame();
        ImGui::NewFrame();

        // --- Playback clock ---
        if (playback.playing)
            playback.beat += static_cast<double>(io.DeltaTime) * doc.juggleParams.bpm / 60.0 *
                             (playback.slow ? static_cast<double>(settings.slowRate) : 1.0);
        // The metronome: a tick as each beat starts while playing (not while scrubbing, stepping
        // or paused), a stronger one on the first beat of each count (or of the pattern, without
        // a count directive).
        {
            const long beatNow = static_cast<long>(std::floor(playback.beat));
            if (playback.playing && settings.metronome && beatNow != sound.metronomeBeat) {
                const long every = doc.patternCount >= 2 ? doc.patternCount : std::max(1, doc.parsed.loop.period);
                sound.tickWanted = ((beatNow % every) + every) % every == 0 ? 2 : 1;
            }
            sound.metronomeBeat = beatNow;
        }

        // --- Keyboard shortcuts (not while typing: the text box has its own undo, etc.) ---
        // While the siteswap grid has the keyboard, keys are its (throws are letters too), so the
        // single-key shortcuts are off, as they are for a text box; Space, Ctrl+S and undo work.
        const bool gridHasKeys = siteswapGridHasKeyboard(gridPane.siteswapGrid);
        // (A text box being typed in elsewhere, as when naming a pattern to save: the grid keeps
        // its focus flag under a dialog, but its keys aren't its.)
        const bool typingElsewhere = io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (gridHasKeys) io.WantTextInput = true;
        // Ctrl+S / Ctrl+Shift+S: Save / Save As (even while typing a siteswap).
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
            commitTyping();
            (io.KeyShift ? ui.saveAsRequested : ui.saveRequested) = true;
        }
        if ((!io.WantTextInput || gridHasKeys) && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)) redo();
            if (stageView.choreographyMode && doc.choreography.active()) {
                // Choreography: Ctrl+1 ... Ctrl+9, Ctrl+0 make the selected spike marks a group
                // (nothing selected: empty it).
                for (int g = 0; g < Choreography::kMarkGroups; ++g)
                    if (ImGui::IsKeyPressed(g == 9 ? ImGuiKey_0 : static_cast<ImGuiKey>(ImGuiKey_1 + g), false))
                        setMarkGroup(g, stageView.selectedMarks);
            } else {
                // Ctrl+1 to Ctrl+6: collapse or expand that juggler's ladder strip.
                const int jugglers =
                    doc.sketching ? doc.sketchLoop.jugglers : (doc.patternValid ? doc.pattern.jugglers : 0);
                for (int j = 0; j < std::min(jugglers, kMaxJugglers); ++j)
                    if (jugglers > 1 && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + j), false))
                        ladderPane.ladderEdit.collapsed[static_cast<size_t>(j)] =
                            !ladderPane.ladderEdit.collapsed[static_cast<size_t>(j)];
            }
        }
        // The 3D mouse (SpaceMouse) always moves the juggler camera, whatever has the keyboard;
        // the cap's deflection sets a speed. (Applied to the camera further down, once the
        // scene is known.)
        const SpaceMouseState spaceMouse = platformSpaceMouse();
        // 3Dconnexion's own software may also send scrolling when the cap moves (its default
        // profile turns a tilt into a zoom). While the cap is moving, and for a moment after,
        // mouse-wheel input is ignored, so it doesn't zoom or scroll on top of what we do.
        {
            bool moving = false;
            for (int i = 0; i < 3; ++i)
                moving = moving || std::fabs(spaceMouse.move[i]) > 0.06f || std::fabs(spaceMouse.turn[i]) > 0.06f;
            if (spaceMouse.present && moving) stageView.spaceMouseQuietUntil = ImGui::GetTime() + 0.3;
            if (ImGui::GetTime() < stageView.spaceMouseQuietUntil) {
                if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)
                    stageView.spaceMouseWheelBlocked = ImGui::GetTime();
                io.MouseWheel = io.MouseWheelH = 0.0f;
            }
        }
        // ?: the keyboard shortcuts (typed as a character, so it's Shift+/ on most keyboards).
        if (!io.WantTextInput)
            for (const ImWchar c : io.InputQueueCharacters)
                if (c == '?') ui.showShortcuts = !ui.showShortcuts;
        if (gridHasKeys && !typingElsewhere && !io.KeyCtrl && !ui.loiter.open &&
            ImGui::IsKeyPressed(ImGuiKey_Space, false))
            playback.playing = !playback.playing;
        if (!io.WantTextInput && !io.KeyCtrl && !ui.loiter.open) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) playback.playing = !playback.playing;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) goToStart();
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) playback.slow = !playback.slow;
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                settings.metronome = !settings.metronome;
                saveSettings(settings);
            }
            // Choreography: 1 ... 9, 0 select a group of spike marks (Shift: add it).
            if (stageView.choreographyMode && doc.choreography.active() && !stageView.markChoice.active)
                for (int g = 0; g < Choreography::kMarkGroups; ++g)
                    if (ImGui::IsKeyPressed(g == 9 ? ImGuiKey_0 : static_cast<ImGuiKey>(ImGuiKey_1 + g), false))
                        selectMarkGroup(g, io.KeyShift);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            // F: frame the selected juggler, or everyone if no one is selected.
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                stageView.frameSelected = stageView.selectedJuggler >= 0;
                stageView.camera.free = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            // C: collapse all the jugglers' ladder strips (or expand them, if all are collapsed).
            // L: dim linked jugglers' throws on the ladder (or not).
            if (ImGui::IsKeyPressed(ImGuiKey_L, false) && doc.parsed.form.hasLinks()) {
                settings.dimLinked = !settings.dimLinked;
                saveSettings(settings);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                const int jugglers =
                    doc.sketching ? doc.sketchLoop.jugglers : (doc.patternValid ? doc.pattern.jugglers : 0);
                if (jugglers > 1) toggleCollapseAll(ladderPane.ladderEdit, jugglers);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
            // M: choreography (Movement, spike Marks: shown and editable); K: the keyframes'
            // spike marks on the ladder; T: the top view.
            if (ImGui::IsKeyPressed(ImGuiKey_M, false)) toggleChoreographyMode();
            if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
                settings.showKeyframeMarks = !settings.showKeyframeMarks;
                saveSettings(settings);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_T, false)) toggleTopView();
        }

        // --- Main menu bar ---
        float menuHeight = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Pattern...")) {
                    if ((doc.patternValid || doc.sketching) &&
                        (doc.savedPatternText != doc.siteswapText || doc.savedChoreography != choreographyText() ||
                         doc.savedThrowNext != throwNextText(doc.throwNext))) {
                        ui.promptForQuit = false;
                        ui.savePromptRequested = true;
                    } else {
                        ui.newPatternDialog.openRequested = true;
                    }
                }
                ImGui::Separator();
                // (A chosen pattern is copied before loading it: loading changes the Recent list
                // the choice may point into.)
                if (ImGui::BeginMenu("JuggleSim Patterns")) {
                    const PatternChoice choice = drawJuggleSimPatternsMenu(
                        library.builtInPatterns, library.juggleSimMenuState, settings.colorVision);
                    if (choice.pattern) {
                        const LibraryPattern chosen = *choice.pattern;
                        loadLibraryPattern(chosen, choice.mine);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("My Patterns")) {
                    const PatternChoice choice = drawMyPatternsMenu(library.myPatterns, library.recentPatterns,
                                                                    library.myMenuState, settings.colorVision);
                    if (choice.pattern) {
                        LibraryPattern chosen = *choice.pattern;
                        if (choice.mine) {  // from Recent: the saved version, if it's still there
                            for (const LibraryPattern& saved : library.myPatterns) {
                                if (saved.displayName() == chosen.displayName()) {
                                    chosen = saved;
                                    break;
                                }
                            }
                        }
                        loadLibraryPattern(chosen, choice.mine);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                {
                    const bool open = openPatternIndex() >= 0;
                    const std::string saveLabel =
                        open ? "Save \"" + shortMenuText(doc.openPatternName) + "\"" : std::string("Save...");
                    if (ImGui::MenuItem(saveLabel.c_str(), "Ctrl+S", false, doc.patternValid || doc.sketching))
                        ui.saveRequested = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip(open ? "Save the changes to this pattern in My Patterns."
                                               : "Save to My Patterns (it isn't one of them yet, so this asks for a name).");
                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, doc.patternValid || doc.sketching))
                        ui.saveAsRequested = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Save to My Patterns under a new name (or over another one).");
                }
                ImGui::MenuItem("Manage My Patterns...", nullptr, &library.manageWindow.open);
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) requestQuit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                    !doc.history.undo.empty() || ladderPane.ladderEdit.chain.active))
                    undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !doc.history.redo.empty())) redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Playback")) {
                if (ImGui::MenuItem(playback.playing ? "Pause" : "Play", "Space"))
                    playback.playing = !playback.playing;
                if (ImGui::MenuItem("Step Forward", "Right")) stepPlayback(kStepBeats);
                if (ImGui::MenuItem("Step Back", "Left")) stepPlayback(-kStepBeats);
                if (ImGui::MenuItem("Step Forward One Beat", "Shift+Right")) stepPlayback(1.0);
                if (ImGui::MenuItem("Step Back One Beat", "Shift+Left")) stepPlayback(-1.0);
                if (ImGui::MenuItem("Go to Beat 1", "B")) goToStart();
                if (ImGui::MenuItem("Slow Motion", "S", playback.slow)) playback.slow = !playback.slow;
                if (ImGui::MenuItem("Metronome", "A", settings.metronome)) {
                    settings.metronome = !settings.metronome;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("A tick on every beat while playing, a stronger one on the first beat of each count.");
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Tweakables")) resetTweakables(doc.juggleParams);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Tempo 150 BPM, dwell 1.60 beats, automatic distance");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Props")) {
                for (int i = 0; i < static_cast<int>(PropType::Count); ++i) {
                    const PropType prop = static_cast<PropType>(i);
                    if (ImGui::MenuItem(propTypeName(prop), nullptr, settings.prop == prop) &&
                        settings.prop != prop) {
                        settings.prop = prop;
                        doc.juggleParams.prop = prop;
                        saveSettings(settings);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Ladder View", "Home over ladder", false,
                                    !ladderViewIsDefault(ladderPane.ladderEdit)))
                    resetLadderView(ladderPane.ladderEdit);
                if (ImGui::MenuItem("Throw Values", "V", settings.showThrowValues)) {
                    settings.showThrowValues = !settings.showThrowValues;
                    saveSettings(settings);
                }
                if (ImGui::MenuItem("Reset Layout", nullptr, false, settings.ladderFraction != 0.5f)) {
                    settings.ladderFraction = 0.5f;
                    saveSettings(settings);
                }
                if (ImGui::MenuItem("Reset Camera", "Home over juggler")) {
                    resetCamera(stageView.camera);
                    stageView.frameSelected = false;
                }
                if (ImGui::MenuItem("Frame All", nullptr, false, stageView.frameSelected || stageView.camera.free)) {
                    stageView.frameSelected = false;
                    stageView.camera.free = false;
                }
                if (ImGui::MenuItem("Frame Selected", "F", false, stageView.selectedJuggler >= 0)) {
                    stageView.frameSelected = true;
                    stageView.camera.free = false;
                }
                if (ImGui::MenuItem("Top View", "T", stageView.camera.topView)) toggleTopView();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Look straight down at the floor (for placing spike marks and jugglers).");
                ImGui::Separator();
                if (ImGui::MenuItem("Choreography", "M", stageView.choreographyMode && doc.choreography.active()))
                    toggleChoreographyMode();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Shows the spike marks on the floor, to move, turn and put jugglers on.\n"
                                      "Pause on a beat, then drag a juggler to where they should be then (onto a\n"
                                      "mark, or anywhere). Click marks to select them (Shift+click: more) and drag one\n"
                                      "to move them all. T: the top view.");
                if (ImGui::MenuItem("Keyframe Marks", "K", settings.showKeyframeMarks)) {
                    settings.showKeyframeMarks = !settings.showKeyframeMarks;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("On the ladder: \"M3\" beside each keyframe on a spike mark.");
                if (ImGui::MenuItem("Path Beat Numbers", nullptr, settings.pathBeatNumbers,
                                    stageView.choreographyMode)) {
                    settings.pathBeatNumbers = !settings.pathBeatNumbers;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Choreography: number every beat along the jugglers' paths.\n"
                                      "Off: only the point under the mouse is numbered.");
                if (ImGui::MenuItem("Clear Choreography", nullptr, false, doc.choreography.active())) {
                    doc.choreography = Choreography();
                    stageView.choreographyMode = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Color by Orbit", "O", settings.colorByOrbit)) {
                    settings.colorByOrbit = !settings.colorByOrbit;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("One color per orbit (the throws a group of props travel round)\n"
                                      "instead of one per prop. Sketches are always colored by path.");
                {
                    const int jugglers =
                        doc.sketching ? doc.sketchLoop.jugglers : (doc.patternValid ? doc.pattern.jugglers : 0);
                    const bool expand = allStripsCollapsed(ladderPane.ladderEdit, jugglers);
                    if (ImGui::MenuItem(expand ? "Expand All Jugglers" : "Collapse All Jugglers", "C", false, jugglers > 1))
                        toggleCollapseAll(ladderPane.ladderEdit, jugglers);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Narrows every juggler's strip on the ladder (their throws still show),\n"
                                          "or widens them all again if they're all narrow.");
                }
                if (ImGui::MenuItem("Dim Linked Jugglers", "L", settings.dimLinked, doc.parsed.form.hasLinks())) {
                    settings.dimLinked = !settings.dimLinked;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Mutes the throws of jugglers who are links (@2[3]: another juggler's\n"
                                      "throws) on the ladder, so the parts written out stand out.");
                if (ImGui::BeginMenu("Color Vision")) {
                    for (int i = 0; i < static_cast<int>(ColorVisionMode::Count); ++i) {
                        const ColorVisionMode mode = static_cast<ColorVisionMode>(i);
                        if (ImGui::MenuItem(colorVisionModeName(mode), nullptr,
                                            settings.colorVision == mode) &&
                            settings.colorVision != mode) {
                            settings.colorVision = mode;
                            saveSettings(settings);
                        }
                    }
                    ImGui::Separator();
                    ImGui::MenuItem("Preview all modes...", nullptr, &ui.showColorPreview);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Sounds", nullptr, settings.sounds)) {
                    settings.sounds = !settings.sounds;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("A short plip when something you just did is refused, or breaks the pattern.");
                ImGui::Separator();
                ImGui::MenuItem("ImGui demo window", nullptr, &ui.showImGuiDemo);
                ImGui::EndMenu();
            }
            if (spaceMouse.present && ImGui::BeginMenu("3D Mouse")) {
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                if (ImGui::SliderFloat("Speed", &settings.spaceMouseSpeed, 0.25f, 4.0f, "%.2fx",
                                       ImGuiSliderFlags_Logarithmic))
                    saveSettings(settings);
                ImGui::TextDisabled("Grab the stage (it makes the camera free). Spin: orbit.\n"
                                    "Tilt toward / away: turn it to see it from above / below.\n"
                                    "Push away / pull toward you: move back / in.\n"
                                    "Slide sideways or up / down: the stage moves with it.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("Keyboard Shortcuts", "?", &ui.showShortcuts);
                ImGui::MenuItem("3D Mouse Diagnostics", nullptr, &ui.showSpaceMouseDiagnostics, spaceMouse.present);
                ImGui::EndMenu();
            }
            menuHeight = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }

        // --- Pane layout ---
        const ImGuiStyle& style = ImGui::GetStyle();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;
        const float top = menuHeight;
        // The ladder pane's width: the user's share of the window (dragging the divider), but
        // never so narrow that either pane is unusable.
        const float minPaneWidth = std::min(ImGui::GetFontSize() * 16.0f, W * 0.5f);
        const float leftWidth = std::floor(std::clamp(W * settings.ladderFraction, minPaneWidth, W - minPaneWidth));
        // A choreography that comes with its floor grid (loaded, or undone to) puts it in place.
        if (doc.choreography.active() && !doc.choreography.grid.empty() &&
            doc.choreography.grid != doc.choreographyGridShown) {
            if (readFloorGridText(doc.choreography.grid, &settings)) saveSettings(settings);
            doc.choreographyGridShown = doc.choreography.grid;
        }
        // The siteswap entry is a pane of its own along the bottom, the full width of the window
        // (long passing patterns need the room); the ladder and juggler panes sit above it.
        int gridRows = 1;
        {
            SiteswapGrid shownGrid;
            if (gridFromText(doc.siteswapText, &shownGrid)) gridRows = std::max(1, shownGrid.jugglers());
        }
        // (The walking rows: one per juggler, as the throws last worked while they have an error.)
        const JugglingLoop choreoRowsLoop = shownLoop();
        const int choreoGridRows = choreoRowsLoop.empty() ? gridRows : choreoRowsLoop.jugglers;
        const float entryHeight =
            siteswapGridHeight(gridRows, doc.choreography.active() ? choreoGridRows : 0) + style.WindowPadding.y * 2.0f;
        const float bodyBottom = H - entryHeight;
        const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;

        // The throwing loop played out (see loop_ops.h): what's thrown, beat by beat, a pass going to
        // whoever walks its target's path (the walking loop, as it last worked) when it lands.
        const JugglingLoop writtenLoop =
            doc.sketching ? doc.sketchLoop : (doc.patternValid ? patternLoop(doc.pattern) : JugglingLoop());
        {
            stage.playPaths = WalkPaths();
            const Choreography& c = stage.walkLoopBroken ? stage.stageChoreography : doc.choreography;
            if (c.active() && !writtenLoop.empty()) {
                const WalkLoop w = walkLoop(c, writtenLoop.jugglers);
                if (w.valid && w.phases > 1) {
                    stage.playPaths.cycle = c.cycle > 0 ? c.cycle : choreographyCycle(writtenLoop, doc.juggleParams);
                    stage.playPaths.path = w.role;
                }
            }
            const ThrowLoopPlan plan = throwLoopPlan(doc.throwNext, writtenLoop.jugglers);
            stage.played = playedLoop(writtenLoop, plan.valid ? plan : ThrowLoopPlan(), stage.playPaths);
            stage.throwLoopBroken = !writtenLoop.empty() && (!plan.valid || !stage.played.ok);
            stage.throwLoopProblem = !plan.valid ? plan.problem : stage.played.problem;
            // (No pattern at all, the throws having an error: the stage keeps the last that worked.)
            // (Pausing as it breaks: editing a throw, you're usually looking at a moment, to see where
            // everyone is. Play again whenever you like.)
            const bool wasBroken = stage.throwsBroken;
            stage.throwsBroken = writtenLoop.empty() && !stage.stagePlayLoop.empty();
            if (stage.throwsBroken && !wasBroken) playback.playing = false;
            if (!writtenLoop.empty()) stage.lastGoodLoop = writtenLoop;
            if (!writtenLoop.empty() &&
                (!stage.throwLoopBroken || stage.stagePlayLoop.jugglers != writtenLoop.jugglers))
                stage.stagePlayLoop = stage.played.loop;
        }

        // Ladder diagram (top of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, top));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyBottom - top));
        // The ladder handles the mouse wheel itself (pan/zoom), so the pane mustn't scroll.
        ladderPane.ladderPreview = false;
        if (ImGui::Begin("Ladder", nullptr, paneFlags | ImGuiWindowFlags_NoScrollWithMouse)) {
            // What the ladder shows and edits: the sketch, or the pattern.
            const JugglingLoop ladderLoop =
                doc.sketching ? doc.sketchLoop : (doc.patternValid ? patternLoop(doc.pattern) : JugglingLoop());
            const LadderToolbarRequest request =
                drawLadderToolbar(ladderLoop, ladderPane.ladderEdit, settings.showThrowValues, settings.colorByOrbit,
                                  doc.parsed.form.hasLinks(), settings.dimLinked);
            if (request.toggleOrbits) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            if (request.toggleCollapseAll) toggleCollapseAll(ladderPane.ladderEdit, ladderLoop.jugglers);
            if (request.toggleDimLinked) {
                settings.dimLinked = !settings.dimLinked;
                saveSettings(settings);
            }
            if (request.resetView) resetLadderView(ladderPane.ladderEdit);
            if (request.toggleValues) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
            if (request.newPeriodBeats > 0 && doc.patternCount >= 2 && request.newPeriodBeats % doc.patternCount != 0) {
                gridPane.siteswapBox.note = "Can't: this is a " + std::to_string(doc.patternCount) +
                                            "-count, so the period goes up and down " +
                                            std::to_string(doc.patternCount) + " beats at a time.";
                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            } else if (request.newPeriodBeats > 0) {
                cancelLadderEdit(ladderPane.ladderEdit);
                commitTyping();
                const JugglingLoop longer = loopWithPeriod(ladderLoop, request.newPeriodBeats);
                // (Written as it is: a period chosen on purpose isn't shortened again.)
                std::string text;
                if (loopToText(longer, &text, &doc.parsed.form)) {
                    const Siteswap check = parseSiteswap(text);
                    if (check.valid || check.sketch) {
                        std::snprintf(doc.siteswapText, sizeof(doc.siteswapText), "%s", text.c_str());
                        doc.parsed = check;
                        doc.patternValid = check.valid;
                        doc.sketching = check.sketch;
                        if (check.valid) doc.pattern = patternFromSiteswap(check);
                        if (check.sketch) doc.sketchLoop = check.loop;
                        recordPattern(text);
                    }
                }
            }
            LadderViewOptions ladderOptions;
            ladderOptions.showValues = settings.showThrowValues;
            ladderOptions.colorByOrbit = settings.colorByOrbit;
            ladderOptions.selectedJuggler = stageView.selectedJuggler;
            ladderOptions.highlightThrow = gridPane.siteswapHoverThrow;
            ladderOptions.relativeTargets = doc.parsed.form.relative;
            ladderOptions.form = &doc.parsed.form;
            ladderOptions.countN = doc.patternCount;
            ladderOptions.writtenPeriod = stage.played.expanded ? stage.played.period : 0;
            ladderOptions.dimLinked = settings.dimLinked;
            ladderOptions.followPlayhead = playback.playing;
            {
                const JugglingLoop spinLoop = doc.patternValid && !doc.sketching ? stage.played.loop : JugglingLoop();
                if (spinLoop != ladderPane.spinCacheLoop || doc.juggleParams.bpm != ladderPane.spinCacheParams.bpm ||
                    doc.juggleParams.dwellBeats != ladderPane.spinCacheParams.dwellBeats ||
                    doc.juggleParams.prop != ladderPane.spinCacheParams.prop ||
                    doc.juggleParams.distance != ladderPane.spinCacheParams.distance) {
                    ladderPane.spinCache = throwSpins(spinLoop, doc.juggleParams);
                    ladderPane.spinCacheLoop = spinLoop;
                    ladderPane.spinCacheParams = doc.juggleParams;
                }
                ladderOptions.spins = &ladderPane.spinCache;
            }
            {
                const JugglingLoop ladderLoop = shownLoop();
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
            LadderEditResult edited =
                drawLadderDiagram(stage.played.expanded ? stage.played.loop : writtenLoop, settings.colorVision,
                                  ladderPane.ladderEdit, playback.beat, ladderOptions);
            // Picking up a throw pauses the jugglers (don't confuse them mid-edit); drawing in a
            // sketch doesn't, so you can watch props pop in as you go.
            if (edited.startedChain) playback.playing = false;
            if (!edited.note.empty()) {
                gridPane.siteswapBox.note = edited.note;
                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            }
            ladderPane.ladderPreview = edited.hasPreview;
            if (ladderPane.ladderPreview) ladderPane.ladderPreviewLoop = edited.preview;
            if (edited.selectJuggler != kNoSelectionChange) {
                stageView.selectedJuggler = edited.selectJuggler;
                if (stageView.selectedJuggler < 0) stageView.frameSelected = false;
            }
            if (edited.committed) {
                const bool wasWorking = doc.parsed.valid || doc.parsed.sketch;
                // An edit to the loop played out goes into the beats written out (if that's where it was).
                JugglingLoop changed = edited.loop;
                std::string why;
                if (stage.played.expanded &&
                    !writtenFromPlayed(stage.played, writtenLoop, edited.loop, stage.playPaths, &changed, &why)) {
                    gridPane.siteswapBox.note = "Can't: " + why;
                    gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                    sound.plipWanted = true;
                } else {
                    applyLoop(changed);
                    if (wasWorking && !doc.parsed.valid && !doc.parsed.sketch)
                        sound.plipWanted = true;  // (it broke it)
                }
            }
            if (edited.goToStart) goToStart();
            if (edited.walkLikeJuggler >= 0) openWalkLike(edited.walkLikeJuggler);
            if (edited.deleteKeyframeJuggler >= 0)
                removeKeyframe(&doc.choreography, edited.deleteKeyframeJuggler, edited.deleteKeyframeBeat);
            // A keyframe's own changes: turning the long way round, or moved to another beat.
            auto keyframeAt = [&](int j, int beat) -> Keyframe* {
                if (j < 0 || j >= static_cast<int>(doc.choreography.keys.size())) return nullptr;
                for (Keyframe& k : doc.choreography.keys[static_cast<size_t>(j)])
                    if (k.beat == beat) return &k;
                return nullptr;
            };
            if (Keyframe* k = keyframeAt(edited.longTurnKeyframeJuggler, edited.longTurnKeyframeBeat)) k->longTurn = !k->longTurn;
            // With rotated repeats, keyframes stay in the beats written out.
            const int writtenBeats = choreographyCycle(shownLoop(), doc.juggleParams);
            auto pastWritten = [&](int beat) {
                if (doc.choreography.repeats() == 1 || beat < writtenBeats) return false;
                gridPane.siteswapBox.note =
                    "Can't: beat " + std::to_string(beat + 1) + " is in the walking loop's copies (only beats 1-" +
                    std::to_string(writtenBeats) + " are written out; the C rows' next column says what comes after).";
                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                return true;
            };
            if (edited.moveKeyframeJuggler >= 0 && pastWritten(edited.moveKeyframeTo)) edited.moveKeyframeJuggler = -1;
            if (edited.loiterJuggler >= 0 && pastWritten(edited.loiterTo)) edited.loiterJuggler = -1;
            if (Keyframe* k = keyframeAt(edited.moveKeyframeJuggler, edited.moveKeyframeFrom)) {
                Keyframe moved = *k;
                moved.beat = edited.moveKeyframeTo;
                removeKeyframe(&doc.choreography, edited.moveKeyframeJuggler, edited.moveKeyframeFrom);
                setKeyframe(&doc.choreography, edited.moveKeyframeJuggler, moved);  // (replacing one already there)
            }
            // Ctrl+dragged: a loiter, the keyframe copied to another beat. If the copy comes first,
            // the juggler arrives there (and any long way round goes with the arrival).
            if (Keyframe* k = keyframeAt(edited.loiterJuggler, edited.loiterFrom)) {
                Keyframe copy = *k;
                copy.beat = edited.loiterTo;
                copy.longTurn = edited.loiterBefore && k->longTurn;
                if (edited.loiterBefore) k->longTurn = false;
                setKeyframe(&doc.choreography, edited.loiterJuggler, copy);
            }
            // Scrubbing in the beat numbers: the playhead follows the mouse, and playback stays
            // paused afterwards.
            if (edited.scrubbing) {
                playback.playing = false;
                playback.beat = edited.scrubBeat;
            }
            // A juggler's menu: Same as... (make them a link) or Unlink (write them out).
            if (edited.linkJuggler >= 0) linkJugglers(edited.linkJuggler, edited.linkTo, edited.linkStart);
            if (edited.unlinkJuggler >= 0) unlinkJugglers(edited.unlinkJuggler);
        }
        ImGui::End();

        // The siteswap grid (along the bottom, full width).
        ImGui::SetNextWindowPos(ImVec2(0.0f, bodyBottom));
        ImGui::SetNextWindowSize(ImVec2(W, entryHeight));
        gridPane.siteswapHoverThrow = -1;
        if (ImGui::Begin("Siteswap entry", nullptr, paneFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ThrowLoopGridView throwView;
            throwView.active = writtenLoop.jugglers > 1;
            {
                const ThrowLoopPlan plan = throwLoopPlan(doc.throwNext, writtenLoop.jugglers);
                for (int j = 0; j < writtenLoop.jugglers; ++j) {
                    const int k =
                        j < static_cast<int>(doc.throwNext.size()) ? doc.throwNext[static_cast<size_t>(j)] : -1;
                    throwView.next.push_back(k >= 0 && k != j ? std::to_string(k + 1) : std::string());
                    throwView.bad.push_back(
                        static_cast<char>(std::find(plan.bad.begin(), plan.bad.end(), j) != plan.bad.end()));
                }
                if (stage.played.expanded) throwView.played = stage.played.loop;
                throwView.part = stage.played.part;
                throwView.clash = stage.played.clash;
            }
            ChoreoGridView choreoView = choreoGridView();
            // Who's on each row at the playhead (the rows are positions; see siteswap_grid.h).
            {
                const int now = static_cast<int>(std::floor(playback.beat));
                throwView.looping = stage.played.part.size() > 1;
                for (int r = 0; r < writtenLoop.jugglers && throwView.looping; ++r) {
                    int who = r;
                    for (int j = 0; j < writtenLoop.jugglers; ++j)
                        if (stage.played.partAt(j, now) == r) who = j;
                    throwView.who.push_back(who == r ? -1 : who);
                }
                choreoView.looping = !stage.playPaths.trivial();
                for (int k = 0; k < static_cast<int>(choreoView.rows.size()) && choreoView.looping; ++k) {
                    const int who = stage.playPaths.walker(k, now);
                    choreoView.who.push_back(who == k ? -1 : who);
                }
            }
            const SiteswapGridResult g =
                drawSiteswapGrid(gridPane.siteswapGrid, doc.siteswapText, doc.parsed, doc.patternCount,
                                 settings.colorVision, playback.beat, choreoView, throwView);
            // The throws' next column: whose throws a juggler takes up after the period. (A change
            // that leaves it not working plips.)
            for (const std::pair<int, int>& e : g.throwNextEdits) {
                if (e.first < 0 || e.first >= kMaxJugglers) continue;
                if (static_cast<int>(doc.throwNext.size()) <= e.first)
                    doc.throwNext.resize(static_cast<size_t>(e.first) + 1, -1);
                doc.throwNext[static_cast<size_t>(e.first)] = e.second;
                const ThrowLoopPlan plan = throwLoopPlan(doc.throwNext, writtenLoop.jugglers);
                if (!plan.valid || !playedLoop(writtenLoop, plan, stage.playPaths).ok) sound.plipWanted = true;
            }
            // The choreography's rows: keyframes typed, walk links, and the beat to show.
            // (Throws and walking changed together, a selection across both: one undo step.)
            const bool together = !g.choreoEdits.empty() && g.commit && g.newCount < 0;
            PatternSettings settingsBeforeBoth;
            if (together) {
                commitTyping();
                recordSettingsChange();
                settingsBeforeBoth = currentPatternSettings();
            }
            for (const ChoreoEdit& e : g.choreoEdits) {
                if (e.juggler < 0) continue;
                if (e.kind == ChoreoEdit::Kind::Set) {
                    Keyframe k;
                    k.beat = e.beat;
                    k.mark = findMarkNamed(doc.choreography, e.mark);
                    if (k.mark < 0) continue;
                    if (e.longTurn >= 0)
                        k.longTurn = e.longTurn != 0;
                    else if (doc.choreography.hasKeys(e.juggler))
                        for (const Keyframe& old : doc.choreography.keys[static_cast<size_t>(e.juggler)])
                            if (old.beat == e.beat) k.longTurn = old.longTurn;
                    setKeyframe(&doc.choreography, e.juggler, k);
                } else if (e.kind == ChoreoEdit::Kind::Rotate) {
                    // A beat (or more) later, round the beats written out.
                    const int written = std::max(1, choreographyCycle(shownLoop(), doc.juggleParams));
                    if (doc.choreography.hasKeys(e.juggler)) {
                        std::vector<Keyframe> keys = doc.choreography.keys[static_cast<size_t>(e.juggler)];
                        doc.choreography.keys[static_cast<size_t>(e.juggler)].clear();
                        for (Keyframe& k : keys) {
                            k.beat = ((k.beat + e.beat) % written + written) % written;
                            setKeyframe(&doc.choreography, e.juggler, k);
                        }
                    }
                } else if (e.kind == ChoreoEdit::Kind::Clear) {
                    removeKeyframe(&doc.choreography, e.juggler, e.beat);
                } else if (e.kind == ChoreoEdit::Kind::Loop) {
                    Continuation k;
                    if (!readContinuation(e.mark, &k)) {
                        gridPane.siteswapBox.note = "\"" + e.mark +
                                                    "\" isn't a row to go on to: a C row's number, then maybe a turn "
                                                    "(2, 2r+1/4, r1/2).";
                        gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                    } else {
                        setContinuation(e.juggler, k);
                    }
                } else if (e.kind == ChoreoEdit::Kind::InsertBeat || e.kind == ChoreoEdit::Kind::DeleteBeat) {
                    for (int i = 0; i < std::max(1, e.count); ++i) walkingBeat(e.beat, e.kind == ChoreoEdit::Kind::InsertBeat);
                } else if (doc.choreography.hasKeys(e.juggler)) {
                    for (Keyframe& k : doc.choreography.keys[static_cast<size_t>(e.juggler)])
                        if (k.beat == e.beat) k.longTurn = !k.longTurn;
                }
            }
            if (g.walkLinkJuggler >= 0 && g.walkLinkLeader >= 0) {
                if (doc.choreography.dependsOn(g.walkLinkLeader, g.walkLinkJuggler)) {
                    gridPane.siteswapBox.note = "Can't: J" + std::to_string(g.walkLinkLeader + 1) +
                                                " already walks like J" + std::to_string(g.walkLinkJuggler + 1) +
                                                " (perhaps through others).";
                    gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                } else {
                    WalkLink l = doc.choreography.link(g.walkLinkJuggler);
                    l.leader = g.walkLinkLeader;
                    l.offset = g.walkLinkOffset;
                    setWalkLink(&doc.choreography, g.walkLinkJuggler, l);
                }
            }
            if (g.walkUnlinkJuggler >= 0) unlinkWalk(g.walkUnlinkJuggler);
            if (g.walkLikeJuggler >= 0) openWalkLike(g.walkLikeJuggler);
            if (g.loopBuilderJuggler >= 0) {
                stageView.loopBuilder.juggler = g.loopBuilderJuggler;
                stageView.loopBuilder.k = doc.choreography.continuation(g.loopBuilderJuggler);
                stageView.loopBuilder.requested = true;
            }
            if (g.scrubBeat >= 0.0) {
                playback.playing = false;
                playback.beat = g.scrubBeat;
            }
            if (g.selectJuggler >= 0 && g.selectJuggler < shownLoop().jugglers && shownLoop().jugglers > 1)
                stageView.selectedJuggler = g.selectJuggler;
            if (g.newCount >= 0) {
                // A new count directive (and the throws moved to fit it, if asked): one undo step.
                commitTyping();
                recordSettingsChange();
                const PatternSettings before = currentPatternSettings();
                doc.patternCount = g.newCount;
                if (g.changed) showPatternText(g.text);
                doc.history.recordWithSettings(g.changed ? g.text : doc.history.current, before,
                                               currentPatternSettings());
                settingsRecorded();
            } else if (together) {
                if (g.commitText != doc.siteswapText) showPatternText(g.commitText);
                doc.history.recordWithSettings(g.commitText, settingsBeforeBoth, currentPatternSettings());
                settingsRecorded();
            } else {
                if (g.commit) {
                    if (g.commitText != doc.siteswapText) showPatternText(g.commitText);
                    recordPattern(g.commitText);
                }
                if (g.changed && g.text != doc.siteswapText) showPatternText(g.text);
            }
            if (g.restart) playback.beat = 0.0;
            if (g.linkJuggler >= 0) linkJugglers(g.linkJuggler, g.linkTo, g.linkStart);
            if (g.unlinkJuggler >= 0) unlinkJugglers(g.unlinkJuggler);
            if (!g.note.empty()) {
                gridPane.siteswapBox.note = g.note;
                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 5.0;
            }
            if (g.plip) sound.plipWanted = true;
            // Resting on a throw describes it and picks it out on the ladder.
            const Siteswap& shown = doc.parsed;
            if (g.hoverJuggler >= 0 && stage.played.expanded && g.hoverJuggler < stage.played.loop.jugglers &&
                g.hoverColumn >= 0) {
                // (The loop played out: the throw made then by whoever's on that row.)
                const int beat = g.hoverColumn % stage.played.loop.period;
                int who = g.hoverJuggler;
                for (int j = 0; j < stage.played.loop.jugglers; ++j)
                    if (stage.played.partAt(j, beat) == g.hoverJuggler) who = j;
                gridPane.siteswapHoverThrow = who * stage.played.loop.period + beat;
                std::string says = describeThrow(stage.played.loop, who, beat);
                if (stage.played.part.size() > 1)
                    says += "\n(T" + std::to_string(g.hoverJuggler + 1) + "'s throw, made by J" + std::to_string(who + 1) + ")";
                ImGui::SetTooltip("%s", says.c_str());
            } else if (g.hoverJuggler >= 0 && !shown.loop.empty() && g.hoverJuggler < shown.loop.jugglers) {
                const int beat = g.hoverBeat % shown.loop.period;
                if (shown.valid || shown.sketch)
                    gridPane.siteswapHoverThrow = g.hoverJuggler * shown.loop.period + beat;
                ImGui::SetTooltip("%s", describeThrow(shown.loop, g.hoverJuggler, beat).c_str());
            }

            // The status line, beside the Count button.
            if (!gridPane.siteswapBox.note.empty() && ImGui::GetTime() < gridPane.siteswapBox.noteUntil)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)), "%s",
                                   gridPane.siteswapBox.note.c_str());
            else if (!gridPane.siteswapBox.info.empty() && ImGui::GetTime() < gridPane.siteswapBox.infoUntil)
                ImGui::TextUnformatted(gridPane.siteswapBox.info.c_str());
            else if (stage.throwLoopBroken)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "The throwing loop doesn't work: %s (the stage shows it as it last worked).",
                                   stage.throwLoopProblem.c_str());
            else if (stage.walkLoopBroken)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "The walking loop doesn't work: %s (the stage shows it as it last worked).",
                                   stage.walkLoopProblem.c_str());
            else if (doc.patternValid && doc.pattern.jugglers > 1)
                ImGui::TextDisabled("%d jugglers, %d objects, period %d", doc.pattern.jugglers, ballCount(doc.pattern),
                                    loopPeriodBeats(doc.pattern));
            else if (doc.patternValid)
                ImGui::TextDisabled("%d objects, period %d", ballCount(doc.pattern), loopPeriodBeats(doc.pattern));
            else if (doc.sketching)
                ImGui::TextDisabled("Sketch: %d throw%s not decided yet, period %d", doc.parsed.openThrows,
                                    doc.parsed.openThrows == 1 ? "" : "s", doc.sketchLoop.period);
            else if (!doc.parsed.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "%s", doc.parsed.error.c_str());
            else
                ImGui::TextUnformatted("");
            // Where the cursor is ("J1, beat 5, right hand: 3p+2"), at the right.
            if (!g.readout.empty()) {
                ImGui::SameLine();
                const float width = ImGui::CalcTextSize(g.readout.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
                ImGui::TextUnformatted(g.readout.c_str());
            }
        }
        ImGui::End();

        // --- The scene at this moment ---
        // The jugglers juggle the pattern, or the sketch as far as it goes (props pop in and
        // vanish where throws aren't decided yet), or, while drawing, what a click would make.
        // (While the throws have an error, the pattern as it last worked, so the stage doesn't go blank.)
        const bool showJugglers = doc.patternValid || doc.sketching || stage.throwsBroken;
        const JugglingLoop loop =
            ladderPane.ladderPreview
                ? ladderPane.ladderPreviewLoop
                : (doc.sketching ? doc.sketchLoop : (doc.patternValid ? patternLoop(doc.pattern) : JugglingLoop()));
        // ...playing the loop out (the throwing loop, as it last worked); or the ladder's preview.
        const JugglingLoop stageLoop = ladderPane.ladderPreview ? loop : stage.stagePlayLoop;
        // Choreography fixes the distance between the jugglers once anything in it has changed
        // (a mark moved, added or deleted, a keyframe): it puts them where they stand, in meters.
        // Until then it's just the default formation, kept up with the distance.
        // A choreography saved without a length gets one now (enough whole periods to hold its
        // keyframes), so it doesn't change when the siteswap does. (It's not an unsaved change.)
        if (!ladderPane.ladderPreview && !loop.empty() && doc.choreography.active() && doc.choreography.cycle == 0) {
            const std::string before = choreographyText();
            doc.choreography.cycle = choreographyCycle(loop, doc.juggleParams);
            if (doc.savedChoreography == before) doc.savedChoreography = choreographyText();
            if (doc.untouchedChoreography == before) doc.untouchedChoreography = choreographyText();
        }
        const bool choreographyLocked = doc.choreography.active() && choreographyText() != doc.untouchedChoreography;
        if (!ladderPane.ladderPreview && !loop.empty() && doc.choreography.active()) {
            if (choreographyLocked) {
                if (loop.jugglers > 1 && doc.choreography.reference > 0.0 &&
                    doc.juggleParams.distance != doc.choreography.reference)
                    doc.juggleParams.distance = doc.choreography.reference;
            } else if (static_cast<int>(doc.choreography.marks.size()) != loop.jugglers ||
                       doc.choreography.cycle != loop.period ||
                       std::fabs(doc.choreography.reference - passingDistance(loop, doc.juggleParams)) > 1e-9) {
                doc.choreography = Choreography();
                ensureSpikeMarks();
            }
        }
        const bool loopComplete = !stageLoop.empty() && openThrowCount(stageLoop) == 0;
        if (stageView.selectedJuggler >= std::max(loop.jugglers, doc.pattern.jugglers) ||
            std::max(loop.jugglers, doc.pattern.jugglers) < 2) {
            stageView.selectedJuggler = -1;
            stageView.frameSelected = false;
        }
        {
            const JugglingLoop shown = shownLoop();
            const WalkLoop w =
                doc.choreography.active() && !shown.empty() ? walkLoop(doc.choreography, shown.jugglers) : WalkLoop();
            stage.walkLoopBroken = !w.valid;
            stage.walkLoopProblem = w.problem;
            if (!stage.walkLoopBroken) stage.stageChoreography = doc.choreography;
        }
        // What the stage is evaluated with: the choreography as it last worked.
        JuggleParams stageParams = doc.juggleParams;
        stageParams.choreography = &stage.stageChoreography;
        JugglerScene scene;
        std::vector<int> styleOfBall;  // with "color by orbit": each prop's orbit
        if (showJugglers) {
            const BallOrbits ballOrbits = loopComplete ? computeBallOrbits(stageLoop) : BallOrbits();
            scene = evaluateScene(stageLoop, ballOrbits, stageParams, playback.beat);
            if (settings.colorByOrbit && loopComplete)
                styleOfBall = orbitOfEachBall(stageLoop, ballOrbits, computeLoopOrbits(stageLoop));
            const int framedJuggler = stageView.frameSelected ? stageView.selectedJuggler : -1;
            auto sameParams = [](const JuggleParams& a, const JuggleParams& b) {
                return a.bpm == b.bpm && a.dwellBeats == b.dwellBeats && a.prop == b.prop && a.distance == b.distance;
            };
            const double now = ImGui::GetTime();
            // (While a spike mark is dragged, the choreography changes every frame: the framing
            // waits till it settles, like for any other change.)
            const std::string choreographyNow =
                doc.choreography.active() ? choreographyToText(doc.choreography) : std::string();
            if (stageLoop != stage.extentsSeenLoop || !sameParams(doc.juggleParams, stage.extentsSeenParams) ||
                choreographyNow != stage.extentsSeenChoreography) {
                stage.extentsSeenLoop = stageLoop;
                stage.extentsSeenParams = doc.juggleParams;
                stage.extentsSeenChoreography = choreographyNow;
                stage.extentsSeenSince = now;
            }
            const bool stale = stageLoop != stage.extentsLoop || !sameParams(doc.juggleParams, stage.extentsParams) ||
                               framedJuggler != stage.extentsJuggler || choreographyNow != stage.extentsChoreography;
            // At once for the first framing and for framing a juggler (a click); otherwise once
            // the pattern has settled.
            // (Not while a juggler or mark is being moved, or the loiter popup is up: the view would
            // shift under the mouse.)
            const bool placing = stageView.jugglerDrag >= 0 || stageView.markDrag >= 0 || ui.loiter.open;
            if (stale && !placing &&
                (!stage.haveExtents || framedJuggler != stage.extentsJuggler ||
                 now - stage.extentsSeenSince >= kExtentsSettleSeconds)) {
                stage.haveExtents = true;
                stage.extents = computeSceneExtents(stageLoop, stageParams, framedJuggler);
                stage.extentsLoop = stageLoop;
                stage.extentsParams = doc.juggleParams;
                stage.extentsJuggler = framedJuggler;
                stage.extentsChoreography = choreographyNow;
            }
        }

        // The stage a free camera orbits within: the jugglers, the spike marks and keyframe
        // spots, with a margin, from the floor to a bit above head height.
        auto stageBox = [&]() {
            StageBox box;
            bool any = false;
            auto include = [&](float x, float z) {
                if (!any) {
                    box.min = Vec3(x, 0.0f, z);
                    box.max = Vec3(x, 2.5f, z);
                    any = true;
                }
                box.min.x = std::min(box.min.x, x);
                box.min.z = std::min(box.min.z, z);
                box.max.x = std::max(box.max.x, x);
                box.max.z = std::max(box.max.z, z);
            };
            for (const JugglerState& js : scene.jugglers) include(js.position.x, js.position.z);
            const float scale = choreographyScale(loop);
            for (const SpikeMark& m : doc.choreography.marks) include(m.x * scale, m.z * scale);
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
            const int walkers = std::max(static_cast<int>(doc.choreography.keys.size()),
                                         static_cast<int>(doc.choreography.links.size()));
            for (int j = 0; j < walkers; ++j)
                for (const Keyframe& k : effectiveKeyframes(doc.choreography, j, cycle)) {
                    float x = 0.0f, z = 0.0f, yaw = 0.0f;
                    keyframeSpot(doc.choreography, k, &x, &z, &yaw);
                    include(x * scale, z * scale);
                }
            if (!any) return StageBox();
            box.min = Vec3(box.min.x - 1.5f, 0.0f, box.min.z - 1.5f);
            box.max = Vec3(box.max.x + 1.5f, 2.5f, box.max.z + 1.5f);
            return box;
        };
        // How far a point (the free camera) is from the nearest thing on stage: a juggler, a
        // prop or a spike mark. Dollying goes at a speed in proportion, so it's quick from far
        // away and fine up close.
        auto nearestToCamera = [&](Vec3 p) {
            float nearest = 1e9f;
            for (const JugglerState& js : scene.jugglers) {
                nearest = std::min(nearest, length(p - js.position));
                nearest = std::min(nearest, length(p - Vec3(js.position.x, 1.25f, js.position.z)));
            }
            for (const BallState& b : scene.balls) nearest = std::min(nearest, length(p - b.center));
            const float scale = choreographyScale(loop);
            for (const SpikeMark& m : doc.choreography.marks)
                nearest = std::min(nearest, length(p - Vec3(m.x * scale, 0.0f, m.z * scale)));
            if (nearest >= 1e9f) nearest = 3.0f;
            return std::max(nearest, 0.3f);
        };

        // Choreography mode: the paths the jugglers walk.
        // (While the throws have an error, the paths of the pattern as it last worked: the stage's.)
        const JugglingLoop& pathLoop = loop.empty() ? stageLoop : loop;
        const bool showPaths = stageView.choreographyMode && doc.choreography.active() && !pathLoop.empty();
        const int pathLength =
            showPaths ? std::max(1, choreographyCycle(pathLoop, doc.juggleParams)) : 0;    // (written out)
        const int pathCycle = choreographyFullCycle(stage.stageChoreography, pathLength);  // (the walking loop too)
        // With rotated repeats, only the beats written out are edited: the rest are them turned.
        // Whether `beat` falls in a rotated repeat (and if so, a note saying so).
        auto inRotatedRepeat = [&](long beat) {
            if (loop.empty() || doc.choreography.repeats() == 1) return false;
            const int written = std::max(1, choreographyCycle(loop, doc.juggleParams));
            const int whole = choreographyFullCycle(doc.choreography, written);
            const int b = static_cast<int>(((beat % whole) + whole) % whole);
            if (b < written) return false;
            gridPane.siteswapBox.note = "Beat " + std::to_string(b + 1) + " is in the walking loop's copies (beats " +
                                        std::to_string(written + 1) + "-" + std::to_string(whole) +
                                        " come from beats 1-" + std::to_string(written) +
                                        " by the C rows' next column): place jugglers on beats 1-" +
                                        std::to_string(written) + " instead.";
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return true;
        };
        const ChoreographyPaths paths = showPaths ? choreographyPaths(stage.stageChoreography, pathLoop.jugglers,
                                                                      pathLength, choreographyScale(pathLoop))
                                                  : ChoreographyPaths();
        int hoveredPathPoint = -1;

        // Mouse input for the 3D view: an invisible window over the juggler pane (the 3D view
        // itself is drawn straight to OpenGL, not by ImGui). Right-drag to orbit, wheel to zoom; with
        // passing patterns, click a juggler to select them (click empty space to deselect) and
        // double-click to frame them (double-click empty space to frame everyone).
        const float transportHeightForInput = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        const ImVec2 viewMin(leftWidth, top);
        const ImVec2 viewSize(W - leftWidth, bodyBottom - top - transportHeightForInput);
        ImGui::SetNextWindowPos(viewMin);
        ImGui::SetNextWindowSize(viewSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Juggler view input", nullptr,
                         paneFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 1.0f && avail.y > 1.0f) {
                // The mouse: the left button acts on things (selects a juggler; in choreography,
                // drags jugglers and spike marks); the right button drags to orbit the camera
                // (Shift: pan; the middle button pans too), and a right-click without dragging opens
                // the floor's menu. Alt/Option+left-drag orbits as well, for a trackpad.
                ImGui::InvisibleButton("##juggler_view", avail,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                           ImGuiButtonFlags_MouseButtonMiddle);
                if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) {
                    stageView.viewPress = -1;
                    stageView.pathPress = -1;
                }
                if (ImGui::IsItemActivated())
                    stageView.viewPress = ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                                              ? 0
                                              : (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? 1 : 2);
                const bool leftPress = stageView.viewPress == 0;
                const float clickSlop = io.MouseDragThreshold;
                const bool dragged =
                    io.MouseDragMaxDistanceSqr[std::max(0, stageView.viewPress)] >= clickSlop * clickSlop;
                // Choreography: the spike marks can be dragged (moved, or turned by the arrow) and
                // clicked (a keyframe there for the selected juggler, on the current beat).
                const bool editingFloor = stageView.choreographyMode && doc.choreography.active();
                const float markScale = choreographyScale(loop);
                const bool canSelect = scene.jugglers.size() > 1;
                if (ImGui::IsItemActivated()) {
                    stageView.viewPan = stageView.viewPress == 2 || (stageView.viewPress == 1 && io.KeyShift);
                    stageView.viewOrbitLeft = stageView.viewPress == 0 && io.KeyAlt;
                }
                // A path point under the mouse (a click on it goes to its beat, with its juggler
                // selected).
                if (showPaths && ImGui::IsItemHovered() && stageView.markDrag < 0 && stageView.jugglerDrag < 0)
                    hoveredPathPoint = pickPathPoint(paths, stageView.lastViewProj, viewMin, viewSize, io.MousePos,
                                                     BodyOccluder(scene, stageView.lastCamera.position));
                if (ImGui::IsItemActivated() && leftPress && !stageView.pressTakenByChoice)
                    stageView.pathPress = hoveredPathPoint;
                const int underMouse = canSelect && ImGui::IsItemHovered()
                                           ? pickJuggler(scene, stageView.lastViewProj, viewMin, viewSize, io.MousePos)
                                           : -1;
                // Any part of a mark can be dragged: anywhere inside its ring moves it, and its
                // arrow turns it. A click on it puts the selected juggler there. Someone standing on
                // it is picked instead only where the mouse is squarely on their body (no slack),
                // so the floor showing inside the ring is the mark's. The arrow beats everything.
                bool onArrow = false;
                Vec3 floorHere(0.0f, 0.0f, 0.0f);
                const bool haveFloor = editingFloor && floorUnderMouse(stageView.lastCamera, stageView.lastAspect,
                                                                       viewMin, viewSize, io.MousePos, &floorHere);
                // (A mark's name comes first: it picks that mark, even among several on one spot.)
                const int labelUnderMouse =
                    editingFloor && ImGui::IsItemHovered() && stageView.markDrag < 0
                        ? markLabelUnder(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                         io.MousePos, markGroupView().badgeH())
                        : -1;
                const int markUnderMouse =
                    labelUnderMouse >= 0 ? labelUnderMouse
                    : editingFloor && ImGui::IsItemHovered() && stageView.markDrag < 0
                        ? pickSpikeMark(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                        io.MousePos, haveFloor, floorHere, &onArrow)
                        : -1;
                const bool onBody =
                    labelUnderMouse < 0 && underMouse >= 0 && markUnderMouse >= 0 && !onArrow &&
                    pickJuggler(scene, stageView.lastViewProj, viewMin, viewSize, io.MousePos, 0.0f) >= 0;
                const bool markRing = markUnderMouse >= 0 && (onArrow || !onBody);
                // A mark (or its arrow) comes before the path points drawn on it (a juggler standing
                // on a mark has a path point in its middle).
                if (markRing) {
                    hoveredPathPoint = -1;
                    if (ImGui::IsItemActivated()) stageView.pathPress = -1;
                }
                // Choosing among marks in the same place: a click on an arrow's tip or its number picks
                // that mark; a click anywhere else keeps the one they're on for now.
                if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) stageView.pressTakenByChoice = false;
                if (stageView.markChoice.active && ImGui::IsItemActivated() && leftPress) {
                    stageView.pressTakenByChoice = true;
                    std::vector<bool> shown;
                    const std::vector<ImVec2> bubbles =
                        markBubbleCenters(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                          &shown, markGroupView().badgeH());
                    int best = -1;
                    float bestD = ImGui::GetFontSize() * 1.4f;
                    for (const int m : stageView.markChoice.marks) {
                        if (m < 0 || m >= static_cast<int>(doc.choreography.marks.size())) continue;
                        Vec3 middle, tip;
                        markGeometry(doc.choreography.marks[static_cast<size_t>(m)], markScale, &middle, &tip);
                        ImVec2 t;
                        if (projectToScreen(stageView.lastViewProj, tip, viewMin, viewSize, &t)) {
                            const float d = std::hypot(t.x - io.MousePos.x, t.y - io.MousePos.y);
                            if (d < bestD) {
                                bestD = d;
                                best = m;
                            }
                        }
                        if (shown[static_cast<size_t>(m)]) {
                            const ImVec2 bc = bubbles[static_cast<size_t>(m)];
                            const float d = std::hypot(bc.x - io.MousePos.x, bc.y - io.MousePos.y);
                            if (d < bestD) {
                                bestD = d;
                                best = m;
                            }
                        }
                    }
                    if (best >= 0) {
                        Keyframe k = stageView.markChoice.pending;
                        k.mark = best;
                        setKeyframe(&doc.choreography, stageView.markChoice.juggler, k);
                        openLoiterPopup(stageView.markChoice.juggler, k, stageView.markChoice.at);
                    }
                    stageView.markChoice.active = false;
                }
                if (editingFloor && ImGui::IsItemActivated() && leftPress && !stageView.pressTakenByChoice) {
                    if (markUnderMouse >= 0 && (underMouse < 0 || markRing)) {
                        stageView.markDrag = markUnderMouse;
                        stageView.markTurning = onArrow;
                        stageView.markForKeyframe = markRing;
                        // Pressed: it's in the selection now (on its own, or added with Shift), so
                        // a drag moves the selection.
                        stageView.markPressWasSelected =
                            std::find(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                      stageView.markDrag) != stageView.selectedMarks.end();
                        if (!stageView.markPressWasSelected) {
                            if (!io.KeyShift) stageView.selectedMarks.clear();
                            stageView.selectedMarks.push_back(stageView.markDrag);
                        }
                        stageView.markDragStart.clear();
                        for (const int m : stageView.selectedMarks)
                            stageView.markDragStart.push_back({doc.choreography.marks[static_cast<size_t>(m)].x,
                                                     doc.choreography.marks[static_cast<size_t>(m)].z});
                    } else if (underMouse >= 0 && !loop.empty() && doc.choreography.link(underMouse).active()) {
                        // A juggler who walks like another goes where the leader goes.
                        gridPane.siteswapBox.note = "J" + std::to_string(underMouse + 1) + " " +
                                                    walkLinkLabel(doc.choreography.link(underMouse)) + ": move J" +
                                                    std::to_string(doc.choreography.link(underMouse).leader + 1) +
                                                    ", or unlink J" + std::to_string(underMouse + 1) +
                                                    " (right-click them: Walk Like...).";
                        gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 5.0;
                    } else if (underMouse >= 0 && !loop.empty() && inRotatedRepeat(std::lround(playback.beat))) {
                        // (Not in a rotated repeat: the note says where to do it.)
                    } else if (underMouse >= 0 && !loop.empty()) {
                        // A juggler: dragging them puts them somewhere new (a keyframe).
                        stageView.jugglerDrag = underMouse;
                        Vec3 at;
                        jugglerPlacementAt(loop, doc.juggleParams, underMouse, std::round(playback.beat), &at,
                                           &stageView.jugglerDragYaw);
                    }
                }
                if (stageView.jugglerDrag >= 0 && !ImGui::IsItemActive()) {
                    // Dropped: how long do they stay?
                    if (ImGui::IsItemDeactivated() && dragged && stageView.jugglerDrag < loop.jugglers &&
                        !loop.empty()) {
                        const int cycle = std::max(1, choreographyCycle(loop, doc.juggleParams));
                        const long beat = std::lround(playback.beat);
                        const int b = static_cast<int>(((beat % cycle) + cycle) % cycle);
                        Keyframe placed;
                        bool found = false;
                        if (stageView.jugglerDrag < static_cast<int>(doc.choreography.keys.size()))
                            for (const Keyframe& k : doc.choreography.keys[static_cast<size_t>(stageView.jugglerDrag)])
                                if (k.beat == b) {
                                    placed = k;
                                    found = true;
                                }
                        // Dropped off every mark: a new mark there, which the keyframe is on (every
                        // keyframe is on a mark).
                        if (found && placed.mark < 0) {
                            placed.mark = markForSpot(&doc.choreography, placed.x, placed.z, placed.yaw);
                            setKeyframe(&doc.choreography, stageView.jugglerDrag, placed);
                        }
                        Vec3 floor;
                        const std::vector<int> here =
                            found && placed.mark >= 0 &&
                                    floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize,
                                                    io.MousePos, &floor)
                                ? marksAround(floor.x, floor.z, markScale)
                                : std::vector<int>();
                        if (here.size() >= 2) {
                            stageView.markChoice = MarkChoice{true, stageView.jugglerDrag, placed, here, io.MousePos};
                        } else if (found) {
                            openLoiterPopup(stageView.jugglerDrag, placed, io.MousePos);
                        }
                    }
                    stageView.jugglerDrag = -1;
                }
                if (stageView.jugglerDrag >= 0 && stageView.jugglerDrag < loop.jugglers && dragged) {
                    // Dragged to the floor under the mouse, on the current beat: onto a spike mark
                    // if it's dropped on one (facing as the mark says), else a spot (snapped to the
                    // floor grid; Shift: freely), facing as they were, which gets a mark of its own
                    // when they're let go.
                    Vec3 floor;
                    if (floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                        &floor)) {
                        const int cycle = std::max(1, choreographyCycle(loop, doc.juggleParams));
                        const long beat = std::lround(playback.beat);
                        Keyframe k;
                        k.beat = static_cast<int>(((beat % cycle) + cycle) % cycle);
                        for (const Keyframe& old :
                             doc.choreography.keys.size() > static_cast<size_t>(stageView.jugglerDrag)
                                 ? doc.choreography.keys[static_cast<size_t>(stageView.jugglerDrag)]
                                 : std::vector<Keyframe>())
                            if (old.beat == k.beat) k.longTurn = old.longTurn;
                        for (int m = 0; m < static_cast<int>(doc.choreography.marks.size()); ++m) {
                            const SpikeMark& sm = doc.choreography.marks[static_cast<size_t>(m)];
                            if (std::hypot(floor.x - sm.x * markScale, floor.z - sm.z * markScale) <= kMarkRadius * markScale)
                                k.mark = m;
                        }
                        if (k.mark < 0) {
                            float x = floor.x, z = floor.z;
                            if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                            k.x = x / markScale;
                            k.z = z / markScale;
                            k.yaw = stageView.jugglerDragYaw;
                        }
                        setKeyframe(&doc.choreography, stageView.jugglerDrag, k);
                        playback.playing = false;
                        playback.beat = static_cast<double>(beat);
                        stageView.selectedJuggler = stageView.jugglerDrag;
                    }
                }
                if (stageView.markDrag >= 0 && stageView.markDrag < static_cast<int>(doc.choreography.marks.size()) &&
                    ImGui::IsItemActive() && dragged) {
                    Vec3 floor;
                    if (floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                        &floor)) {
                        SpikeMark& m = doc.choreography.marks[static_cast<size_t>(stageView.markDrag)];
                        if (stageView.markTurning) {
                            // Turned to point at the mouse: in 15-degree steps (Shift: freely). With
                            // the radial grid, the steps are counted from facing the middle, so one of
                            // them is along the mark's spoke, whatever the spokes.
                            float yaw = std::atan2(floor.x - m.x * markScale, floor.z - m.z * markScale);
                            if (!io.KeyShift) {
                                const float step = kPi / 12.0f;
                                const float from = settings.floorGrid == FloorGridType::Radial && std::hypot(m.x, m.z) > 1e-3f
                                                       ? std::atan2(-m.x, -m.z)
                                                       : 0.0f;
                                yaw = from + std::round((yaw - from) / step) * step;
                            }
                            m.yaw = yaw;
                        } else {
                            // Moved to the mouse: snapped to the floor grid (Shift: freely); the rest
                            // of the selection moves with it, keeping their shape.
                            float x = floor.x, z = floor.z;
                            if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                            const auto here = std::find(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                                        stageView.markDrag);
                            const size_t me = static_cast<size_t>(here - stageView.selectedMarks.begin());
                            if (here == stageView.selectedMarks.end() ||
                                stageView.markDragStart.size() != stageView.selectedMarks.size()) {
                                m.x = x / markScale;
                                m.z = z / markScale;
                            } else {
                                const float dx = x / markScale - stageView.markDragStart[me].first;
                                const float dz = z / markScale - stageView.markDragStart[me].second;
                                for (size_t i = 0; i < stageView.selectedMarks.size(); ++i) {
                                    const int k = stageView.selectedMarks[i];
                                    if (k < 0 || k >= static_cast<int>(doc.choreography.marks.size())) continue;
                                    doc.choreography.marks[static_cast<size_t>(k)].x =
                                        stageView.markDragStart[i].first + dx;
                                    doc.choreography.marks[static_cast<size_t>(k)].z =
                                        stageView.markDragStart[i].second + dz;
                                }
                            }
                        }
                    }
                }
                const bool cameraDrag =
                    ImGui::IsItemActive() && stageView.markDrag < 0 && stageView.jugglerDrag < 0 &&
                    stageView.viewPress >= 0 && (stageView.viewPress != 0 || stageView.viewOrbitLeft) &&
                    ImGui::IsMouseDragging(static_cast<ImGuiMouseButton>(stageView.viewPress), 0.0f);
                const bool orbiting = cameraDrag && !stageView.viewPan;
                const bool panning = cameraDrag && stageView.viewPan;
                if (orbiting) {
                    const ImVec2 d = io.MouseDelta;
                    // In the top view, orbiting turns the view round the vertical (it stays
                    // looking straight down).
                    if (stageView.camera.free) {
                        if (!stageView.cameraOrbiting)
                            stageView.orbitCenter =
                                freeOrbitCenter(stageView.lastCamera, scene, stageView.selectedJuggler, stageBox());
                        orbitFreeCamera(stageView.camera, stageView.orbitCenter, -d.x * 0.008f, d.y * 0.008f);
                    } else {
                        stageView.camera.yaw -= d.x * 0.008f;
                        if (!stageView.camera.topView)
                            stageView.camera.pitch = std::clamp(stageView.camera.pitch + d.y * 0.008f, -1.45f, 1.45f);
                    }
                }
                if (panning) {
                    // Panning drags the floor under the mouse along with it (and frees the camera).
                    if (!stageView.camera.free) makeCameraFree(stageView.camera, stageView.lastCamera);
                    if (!stageView.cameraPanning) {
                        Vec3 floor;
                        stageView.panDepth = floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin,
                                                             viewSize, io.MousePos, &floor)
                                                 ? std::min(length(floor - stageView.lastCamera.position), 40.0f)
                                                 : length(freeOrbitCenter(stageView.lastCamera, scene,
                                                                          stageView.selectedJuggler, stageBox()) -
                                                          stageView.lastCamera.position);
                    }
                    const float metersPerPixel =
                        2.0f * stageView.panDepth * std::tan(kCameraFovY * 0.5f) / std::max(1.0f, viewSize.y);
                    Vec3 forward, right, up;
                    freeCameraAxes(stageView.camera, &forward, &right, &up);
                    stageView.camera.freePos = stageView.camera.freePos - right * (io.MouseDelta.x * metersPerPixel) +
                                     up * (io.MouseDelta.y * metersPerPixel);
                }
                stageView.cameraOrbiting = orbiting;
                stageView.cameraPanning = panning;
                // A click is a press and release without dragging (dragging orbits).
                if (stageView.pathPress >= 0 && stageView.pathPress < static_cast<int>(paths.points.size()) &&
                    ImGui::IsItemDeactivated() && leftPress && !dragged) {
                    // A click on a path point: its beat (in the cycle playing now), its juggler selected.
                    const PathPoint& point = paths.points[static_cast<size_t>(stageView.pathPress)];
                    if (loop.jugglers > 1) stageView.selectedJuggler = point.juggler;
                    playback.playing = false;
                    playback.beat = std::floor(playback.beat / pathCycle) * pathCycle + point.firstBeat;
                    stageView.markDrag = -1;
                    stageView.jugglerDrag = -1;
                    stageView.pathPress = -1;
                } else if (stageView.markDrag >= 0 && ImGui::IsItemDeactivated() && leftPress && !dragged &&
                           !stageView.markForKeyframe) {
                    stageView.selectedJuggler = underMouse;  // a click on someone standing on a mark
                    stageView.markDrag = -1;
                } else if (stageView.markDrag >= 0 && ImGui::IsItemDeactivated()) {
                    if (!dragged) {
                        // A click on a mark selects it (Shift: adds it, or takes it away if it was
                        // already in), and no juggler. (A keyframe: drag a juggler onto it.)
                        if (io.KeyShift) {
                            if (stageView.markPressWasSelected)
                                stageView.selectedMarks.erase(
                                    std::remove(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                                stageView.markDrag),
                                    stageView.selectedMarks.end());
                        } else {
                            stageView.selectedMarks.assign(1, stageView.markDrag);
                        }
                        stageView.selectedJuggler = -1;
                        stageView.frameSelected = false;
                    }
                    stageView.markDrag = -1;
                } else if (canSelect && ImGui::IsItemDeactivated() && leftPress && !dragged &&
                           !stageView.pressTakenByChoice) {
                    stageView.selectedJuggler = underMouse;
                    stageView.selectedMarks.clear();
                    if (underMouse < 0) stageView.frameSelected = false;
                }
                // Right-click (without dragging) the floor: add a mark there, or (on a mark) its menu.
                if (editingFloor && ImGui::IsItemDeactivated() && stageView.viewPress == 1 && !dragged) {
                    stageView.markMenuMark = markRing ? markUnderMouse : -1;
                    Vec3 floor(0.0f, 0.0f, 0.0f);
                    if (stageView.markMenuMark >= 0 || underMouse >= 0 ||
                        floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                        &floor)) {
                        stageView.markMenuPoint = Vec3(floor.x / markScale, 0.0f, floor.z / markScale);
                        stageView.floorMenuJuggler = underMouse;
                        ImGui::OpenPopup("##floor_menu");
                    }
                }
                if (ImGui::BeginPopup("##floor_menu")) {
                    if (stageView.floorMenuJuggler >= 0 && stageView.floorMenuJuggler < loop.jugglers &&
                        loop.jugglers > 1) {
                        const std::string how = walkLinkLabel(doc.choreography.link(stageView.floorMenuJuggler));
                        ImGui::TextDisabled("Juggler %d%s%s", stageView.floorMenuJuggler + 1, how.empty() ? "" : ": ",
                                            how.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Walk Like...")) openWalkLike(stageView.floorMenuJuggler);
                        ImGui::Separator();
                    }
                    if (stageView.markMenuMark >= 0 &&
                        stageView.markMenuMark < static_cast<int>(doc.choreography.marks.size())) {
                        ImGui::TextDisabled(
                            "Spike mark %s",
                            doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)].name.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Face the Middle")) {
                            SpikeMark& m = doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)];
                            if (std::hypot(m.x, m.z) > 1e-3f) m.yaw = std::atan2(-m.x, -m.z);
                        }
                        if (ImGui::MenuItem("Rename...")) {
                            ui.renameMark.mark = stageView.markMenuMark;
                            ui.renameMark.requested = true;
                        }
                        if (ImGui::MenuItem("Delete Spike Mark")) {
                            std::string why;
                            if (!removeSpikeMark(&doc.choreography, stageView.markMenuMark,
                                                 loop.empty() ? 0 : loop.jugglers, &why)) {
                                gridPane.siteswapBox.note = why;
                                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                            } else {
                                stageView.selectedMarks.clear();
                            }
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Only a mark nobody stands on (the others keep their names).");
                        if (ImGui::MenuItem("Add Another Spike Mark Here")) {
                            // Same place, facing the other way for now (so its arrow and name are
                            // clear of the first's), and selected, ready to be turned by its arrow.
                            SpikeMark copy = doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)];
                            copy.name.clear();
                            copy.yaw = std::remainder(copy.yaw + kPi, 2.0f * kPi);
                            const int added = addSpikeMark(&doc.choreography, copy);
                            if (added >= 0) {
                                stageView.selectedMarks.assign(1, added);
                                stageView.selectedJuggler = -1;
                            }
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("A second mark on the same spot, facing the other way and selected: drag its\n"
                                              "arrow to turn it. Putting a juggler there then asks which mark they stand on.");
                    } else if (stageView.floorMenuJuggler < 0) {
                        if (ImGui::MenuItem("Add Spike Mark Here")) {
                            SpikeMark m;
                            float x = stageView.markMenuPoint.x * markScale, z = stageView.markMenuPoint.z * markScale;
                            snapToFloorGrid(settings, &x, &z);
                            m.x = x / markScale;
                            m.z = z / markScale;
                            m.yaw = std::hypot(m.x, m.z) > 1e-3f ? std::atan2(-m.x, -m.z) : 0.0f;  // facing the middle
                            const int added = addSpikeMark(&doc.choreography, m);
                            if (added >= 0) {  // (selected, to see which it is and turn it)
                                stageView.selectedMarks.assign(1, added);
                                stageView.selectedJuggler = -1;
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
                // Things that can be dragged show it: a hand over a juggler (in choreography).
                if (editingFloor && stageView.jugglerDrag >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                else if (editingFloor && underMouse >= 0 && stageView.markDrag < 0 && !orbiting)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (editingFloor && (markUnderMouse >= 0 || stageView.markDrag >= 0))
                    ImGui::SetMouseCursor(stageView.markTurning && stageView.markDrag >= 0 ? ImGuiMouseCursor_ResizeAll
                                                                                           : ImGuiMouseCursor_Hand);
                if (hoveredPathPoint >= 0 && !ImGui::IsItemActive()) {
                    const PathPoint& point = paths.points[static_cast<size_t>(hoveredPathPoint)];
                    const std::string beats = pathPointBeats(point, pathCycle);
                    ImGui::SetTooltip("J%d, beat%s %s\nClick to go to %s, J%d selected", point.juggler + 1,
                                      point.beats > 1 ? "s" : "", beats.c_str(),
                                      point.beats > 1 ? "its first beat" : "that beat", point.juggler + 1);
                } else if (editingFloor && markUnderMouse >= 0 && !ImGui::IsItemActive() && (markRing || onArrow)) {
                    if (onArrow)
                        ImGui::SetTooltip("Spike mark %s: drag the arrow to turn it (Shift: freely)",
                                          doc.choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
                    else
                        ImGui::SetTooltip("Spike mark %s: drag to move it (it snaps to the grid; Shift: freely)\n"
                                          "Click to select it, Shift+click to add it to the selection: dragging one\n"
                                          "moves them all. To put a juggler here, drag them onto it.\n"
                                          "Right-click for more",
                                          doc.choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
                }
                if (canSelect && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    stageView.selectedJuggler = underMouse;
                    stageView.frameSelected = underMouse >= 0;
                    stageView.camera.free = false;
                }
                if (ImGui::IsItemHovered()) {
                    if (io.MouseWheel != 0.0f && stageView.camera.free) {
                        // A free camera moves along its view, more slowly the nearer it is to
                        // something.
                        Vec3 forward, right, up;
                        freeCameraAxes(stageView.camera, &forward, &right, &up);
                        stageView.camera.freePos =
                            stageView.camera.freePos +
                            forward * (io.MouseWheel * 0.15f * nearestToCamera(stageView.camera.freePos));
                    } else if (io.MouseWheel != 0.0f) {
                        stageView.camera.zoom =
                            std::clamp(stageView.camera.zoom * std::pow(0.88f, io.MouseWheel), 0.15f, 6.0f);
                    }
                    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home)) {
                        resetCamera(stageView.camera);
                        stageView.frameSelected = false;
                    }
                    // Paused, resting on a club or ring in the air: its throw and spin rate.
                    bool propTip = false;
                    if (!playback.playing && scene.prop != PropType::Ball && !ImGui::IsItemActive() && hoveredPathPoint < 0) {
                        const BallState* nearest = nullptr;
                        float nearestD = 22.0f;  // pixels
                        for (const BallState& b : scene.balls) {
                            ImVec2 at;
                            if (!b.inFlight || b.flightBeats <= 0.0f ||
                                !projectToScreen(stageView.lastViewProj, b.center, viewMin, viewSize, &at))
                                continue;
                            const float d = std::hypot(at.x - io.MousePos.x, at.y - io.MousePos.y);
                            if (d < nearestD) {
                                nearestD = d;
                                nearest = &b;
                            }
                        }
                        if (nearest) {
                            static const char* const kNames[] = {"flat (no spin)", "a single", "a double", "a triple", "a quad"};
                            const std::string spinName = nearest->spins >= 0 && nearest->spins <= 4
                                                             ? std::string(kNames[nearest->spins])
                                                             : std::to_string(nearest->spins) + " spins";
                            const std::string who = nearest->thrower == nearest->catcher
                                                        ? std::string()
                                                        : ", J" + std::to_string(nearest->thrower + 1) + " to J" +
                                                              std::to_string(nearest->catcher + 1);
                            ImGui::SetTooltip("A %d%s, %s\n%.2f spins a beat (%d in %.2f beats in the air)",
                                              nearest->throwValue, who.c_str(), spinName.c_str(),
                                              static_cast<double>(nearest->spins) / static_cast<double>(nearest->flightBeats),
                                              nearest->spins, static_cast<double>(nearest->flightBeats));
                            propTip = true;
                        }
                    }
                    // A hint, only after the mouse has rested here a moment.
                    if (!propTip && hoveredPathPoint < 0 && !ImGui::IsItemActive() && (markUnderMouse < 0 || underMouse >= 0) &&
                        !(markUnderMouse >= 0 && markRing) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        if (underMouse >= 0 && editingFloor)
                            ImGui::SetTooltip("Juggler %d\nClick to select, double-click to frame\n"
                                              "Drag to put them somewhere else on this beat (a keyframe; drop on a mark to use it)",
                                              underMouse + 1);
                        else if (underMouse >= 0)
                            ImGui::SetTooltip("Juggler %d\nClick to select, double-click to frame", underMouse + 1);
                        else if (canSelect)
                            ImGui::SetTooltip("Right-drag (or Alt+drag) to orbit, middle-drag or Shift+right-drag to pan, wheel to zoom, Home to reset\n"
                                              "Click a juggler to select them, double-click to frame them");
                        else
                            ImGui::SetTooltip("Right-drag (or Alt+drag) to orbit, middle-drag or Shift+right-drag to pan, wheel to zoom, Home to reset");
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // The divider between the ladder and juggler panes: drag to resize them, double-click
        // to share the width equally again. (A thin window of its own over the boundary, so it
        // gets the mouse before either pane.)
        {
            const float grab = 4.0f;  // half the width that catches the mouse
            ImGui::SetNextWindowPos(ImVec2(leftWidth - grab, top));
            ImGui::SetNextWindowSize(ImVec2(grab * 2.0f, bodyBottom - top));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##divider", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::InvisibleButton("##split", ImVec2(grab * 2.0f, bodyBottom - top));
                const bool hovered = ImGui::IsItemHovered();
                const bool active = ImGui::IsItemActive();
                if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                    settings.ladderFraction = std::clamp(io.MousePos.x / W, 0.1f, 0.9f);
                if (ImGui::IsItemDeactivated()) saveSettings(settings);
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    settings.ladderFraction = 0.5f;
                    saveSettings(settings);
                }
                if (hovered || active)
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(leftWidth, top), ImVec2(leftWidth, bodyBottom),
                                                        ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive
                                                                                  : ImGuiCol_SeparatorHovered),
                                                        2.0f);
                if (hovered && !active && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Drag to resize the panes; double-click to share the width equally");
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }

        // Tweakables, in the top-right corner of the juggler pane (over the input window, which
        // never comes to the front).
        const float panelMargin = style.WindowPadding.x;
        float tweakablesBottom = top + panelMargin;
        if (drawTweakablesPanel(doc.juggleParams, loop, settings.tempoPanelCollapsed,
                                ImVec2(W - panelMargin, top + panelMargin), ui.tweakablesPanel, choreographyLocked,
                                &tweakablesBottom))
            saveSettings(settings);
        stageView.groupChipHovered = -1;  // (set again below while the panel is up)
        if (stageView.choreographyMode && doc.choreography.active() && !loop.empty()) {
            int newLength = -1;

            const std::string gridBefore = floorGridText(settings);
            ui.choreographyPanel.groupSizes.assign(Choreography::kMarkGroups, 0);
            for (int g = 0; g < Choreography::kMarkGroups; ++g)
                ui.choreographyPanel.groupSizes[static_cast<size_t>(g)] =
                    static_cast<int>(doc.choreography.group(g).size());
            if (drawChoreographyPanel(settings, ImVec2(W - panelMargin, tweakablesBottom + panelMargin * 0.5f),
                                      ui.choreographyPanel, choreographyCycle(loop, doc.juggleParams), &newLength,
                                      doc.choreography.repeats(), loop.period))
                saveSettings(settings);
            stageView.groupChipHovered = ui.choreographyPanel.hoveredGroup;
            if (ui.choreographyPanel.clickedGroup >= 0) selectMarkGroup(ui.choreographyPanel.clickedGroup, false);
            if (ui.choreographyPanel.addedGroup >= 0) selectMarkGroup(ui.choreographyPanel.addedGroup, true);
            if (ui.choreographyPanel.emptiedGroup >= 0)
                setMarkGroup(ui.choreographyPanel.emptiedGroup, std::vector<int>());
            // The grid changed: it's the choreography's (saved with the pattern). (A choreography
            // nobody has changed yet stays that way: the grid doesn't fix the passing distance.)
            if (floorGridText(settings) != gridBefore) {
                const bool untouched = choreographyText() == doc.untouchedChoreography;
                doc.choreography.grid = floorGridText(settings);
                doc.choreographyGridShown = doc.choreography.grid;
                if (untouched) doc.untouchedChoreography = choreographyText();
            }
            if (newLength > 0) {
                // Shorter than a keyframe's beat: ask first (those keyframes go).
                bool strands = false;
                for (const std::vector<Keyframe>& keys : doc.choreography.keys)
                    for (const Keyframe& k : keys) strands = strands || k.beat >= newLength;
                if (strands) {
                    ui.pendingChoreographyLength = newLength;
                    ImGui::OpenPopup("Shorten the choreography?");
                } else {
                    doc.choreography.cycle = newLength;
                }
            }
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Shorten the choreography?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            std::string gone;
            int count = 0;
            for (size_t j = 0; j < doc.choreography.keys.size(); ++j)
                for (const Keyframe& k : doc.choreography.keys[j])
                    if (k.beat >= ui.pendingChoreographyLength) {
                        gone += (count++ == 0 ? "" : ", ") + std::string("J") + std::to_string(j + 1) + " on beat " +
                                std::to_string(k.beat + 1);
                    }
            ImGui::PushTextWrapPos(em * 30.0f);
            ImGui::Text("A %d-beat choreography has no room for %s %s. Delete %s?", ui.pendingChoreographyLength,
                        count == 1 ? "this keyframe:" : "these keyframes:", gone.c_str(), count == 1 ? "it" : "them");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button("Shorten", ImVec2(em * 7.0f, 0.0f))) {
                for (std::vector<Keyframe>& keys : doc.choreography.keys)
                    keys.erase(
                        std::remove_if(keys.begin(), keys.end(),
                                       [&](const Keyframe& k) { return k.beat >= ui.pendingChoreographyLength; }),
                        keys.end());
                doc.choreography.cycle = ui.pendingChoreographyLength;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Warnings, in the juggler pane under the pattern's name, until dismissed. A dismissed
        // warning comes back if it goes away and something brings it back (its key includes
        // what it's about).
        {
            std::vector<std::pair<std::string, std::string>> warnings;  // key, text
            if (doc.choreography.active() && !loop.empty() && !ladderPane.ladderPreview) {
                const int cycle = choreographyFullCycle(doc.choreography, choreographyCycle(loop, doc.juggleParams));
                const int period = loop.period;
                if (cycle > 0 && period > 0 && cycle % period != 0 && period % cycle != 0) {
                    long a = cycle, b = period;
                    while (b != 0) {
                        const long r = a % b;
                        a = b;
                        b = r;
                    }
                    const long together = static_cast<long>(cycle) / a * period;
                    warnings.emplace_back("lengths:" + std::to_string(period) + ":" + std::to_string(cycle),
                                          "The pattern repeats every " + std::to_string(period) +
                                              " beats and the choreography every " + std::to_string(cycle) +
                                              ", so they only line up again every " + std::to_string(together) +
                                              " beats.");
                }
            }
            for (std::set<std::string>::iterator it = ui.dismissedWarnings.begin(); it != ui.dismissedWarnings.end();) {
                bool stillThere = false;
                for (const std::pair<std::string, std::string>& w : warnings) stillThere = stillThere || w.first == *it;
                it = stillThere ? std::next(it) : ui.dismissedWarnings.erase(it);
            }
            std::vector<std::pair<std::string, std::string>> shown;
            for (const std::pair<std::string, std::string>& w : warnings)
                if (ui.dismissedWarnings.count(w.first) == 0) shown.push_back(w);
            if (!shown.empty()) {
                const float em = ImGui::GetFontSize();
                ImGui::SetNextWindowPos(ImVec2(viewMin.x + panelMargin, viewMin.y + panelMargin + kTitleFontSize + em * 0.6f));
                ImGui::SetNextWindowBgAlpha(0.88f);
                const ImU32 warn = errorTextColor(settings.colorVision);
                ImGui::PushStyleColor(ImGuiCol_Border, warn);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
                if (ImGui::Begin("##warnings", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
                    for (const std::pair<std::string, std::string>& w : shown) {
                        ImGui::PushID(w.first.c_str());
                        ImGui::PushFont(gfx.titleFont, kTitleFontSize);
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(warn), "!");
                        ImGui::PopFont();
                        ImGui::SameLine();
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + em * 24.0f);
                        ImGui::TextUnformatted(w.second.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Dismiss")) ui.dismissedWarnings.insert(w.first);
                        ImGui::PopID();
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
        }

        // Transport bar along the bottom of the juggler pane (right half, above the siteswap entry).
        const float transportHeight = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(leftWidth, bodyBottom - transportHeight));
        ImGui::SetNextWindowSize(ImVec2(W - leftWidth, transportHeight));
        if (ImGui::Begin("Transport", nullptr, paneFlags)) {
            const float h = ImGui::GetFrameHeight();
            if (transportButton("##tostart", TransportIcon::ToStart, h)) goToStart();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to beat 1 (B)");
            ImGui::SameLine();
            if (transportButton("##stepback", TransportIcon::StepBack, h))
                stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step back 1/12 beat (Left)\nShift: one beat (Shift+Left)");
            ImGui::SameLine();
            if (transportButton("##playpause", playback.playing ? TransportIcon::Pause : TransportIcon::Play, h))
                playback.playing = !playback.playing;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(playback.playing ? "Pause (Space)" : "Play (Space)");
            ImGui::SameLine();
            if (transportButton("##stepforward", TransportIcon::StepForward, h))
                stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step forward 1/12 beat (Right)\nShift: one beat (Shift+Right)");
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            // The beat (beat 0 is "beat 1", as on the ladder): drag it to scrub, or double-click
            // (or Ctrl+click) it to type a beat to go to. Either pauses, as stepping does.
            {
                double shown = playback.beat + 1.0;
                ImGui::SetNextItemWidth(h * 5.0f);
                if (ImGui::DragScalar("##beat", ImGuiDataType_Double, &shown, 0.02f, nullptr, nullptr, "Beat %.2f")) {
                    playback.beat = shown - 1.0;
                    playback.playing = false;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Drag to move through time; double-click to type a beat.\n"
                                      "On the ladder, click or drag in the beat numbers.");
            }
            // Slow motion: on/off, and how slow.
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            {
                const bool slow = playback.slow;
                if (slow) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button("Slow")) playback.slow = !playback.slow;
                if (slow) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion (S), at the speed set beside it");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(h * 4.5f);
                if (ImGui::SliderFloat("##slowrate", &settings.slowRate, 0.05f, 0.5f, "%.2fx")) {
                    playback.slow = true;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow-motion speed: 0.05 to 0.5 of normal. Ctrl+click to type one.");
            }
        }
        ImGui::End();

        if (ui.showColorPreview) drawColorVisionPreview(&ui.showColorPreview, settings.colorVision);

        // Pattern library dialogs.
        const SavePatternRequest saveRequest =
            drawSavePatternDialog(library.saveDialog, doc.siteswapText, settingsForSaving(), library.myPatterns);
        if (saveRequest.save && (doc.patternValid || doc.sketching)) {
            storeMyPattern(saveRequest.name, saveRequest.replace);
            if (ui.newAfterSave) ui.newPatternDialog.openRequested = true;
            if (ui.quitAfterSave) ui.done = true;
            ui.newAfterSave = ui.quitAfterSave = false;
        } else if ((ui.newAfterSave || ui.quitAfterSave) && !library.saveDialog.openRequested &&
                   !ImGui::IsPopupOpen("Save to My Patterns")) {
            ui.newAfterSave = ui.quitAfterSave = false;  // the save was cancelled: so is the new pattern (or quitting)
        }
        const ManagePatternsRequest manageRequest =
            drawManagePatternsWindow(library.manageWindow, library.myPatterns, settings.colorVision);
        if (manageRequest.load >= 0) {
            const LibraryPattern chosen = library.myPatterns[static_cast<size_t>(manageRequest.load)];
            loadLibraryPattern(chosen, true);
        }
        if (manageRequest.rename >= 0) {
            LibraryPattern& renamed = library.myPatterns[static_cast<size_t>(manageRequest.rename)];
            const std::string oldName = renamed.displayName();
            const bool wasOpen = oldName == doc.openPatternName;
            renamed.name = manageRequest.newName;
            if (wasOpen) doc.openPatternName = renamed.displayName();
            saveMyPatterns();
            refreshRecentPattern(&library.recentPatterns, oldName, renamed);
            saveRecentPatterns(library.recentPatterns);
        }
        if (manageRequest.remove >= 0) {
            const std::string removedName = library.myPatterns[static_cast<size_t>(manageRequest.remove)].displayName();
            if (removedName == doc.openPatternName) doc.openPatternName.clear();
            library.myPatterns.erase(library.myPatterns.begin() + manageRequest.remove);
            saveMyPatterns();
            removeRecentPattern(&library.recentPatterns, removedName);
            saveRecentPatterns(library.recentPatterns);
        }
        // File > Save / Save As.
        if (ui.saveRequested && (doc.patternValid || doc.sketching)) {
            if (openPatternIndex() >= 0) {
                storeMyPattern(doc.openPatternName, true);
                gridPane.siteswapBox.info = "Saved \"" + doc.openPatternName + "\" to My Patterns.";
                gridPane.siteswapBox.infoUntil = ImGui::GetTime() + 3.0;
            } else {
                ui.saveAsRequested = true;
            }
        }
        if (ui.saveAsRequested && (doc.patternValid || doc.sketching)) {
            std::snprintf(library.saveDialog.name, sizeof(library.saveDialog.name), "%s",
                          doc.openPatternName.empty() ? doc.siteswapText : doc.openPatternName.c_str());
            library.saveDialog.openRequested = true;
        }
        ui.saveRequested = ui.saveAsRequested = false;
        if (!ui.fileError.empty()) ImGui::OpenPopup("Couldn't save");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Couldn't save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(ui.fileError.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
                ui.fileError.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ui.showImGuiDemo) ImGui::ShowDemoWindow(&ui.showImGuiDemo);
        if (ui.showShortcuts) drawShortcutsWindow(&ui.showShortcuts);
        // Choosing among marks in the same place (see MarkChoice): say so, until done (Esc: keep
        // the one they're on).
        if (stageView.markChoice.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || !stageView.choreographyMode) {
                stageView.markChoice.active = false;
            } else {
                gridPane.siteswapBox.info =
                    "Spike marks " +
                    [&]() {
                        std::string list;
                        for (size_t i = 0; i < stageView.markChoice.marks.size(); ++i)
                            list += (i == 0 ? "" : (i + 1 == stageView.markChoice.marks.size() ? " and " : ", ")) +
                                    markNameOf(stageView.markChoice.marks[i]);
                        return list;
                    }() +
                    " are here: click the arrow (or name) of the one J" +
                    std::to_string(stageView.markChoice.juggler + 1) + " stands on. (Esc: mark " +
                    markNameOf(stageView.markChoice.pending.mark) + ".)";
                gridPane.siteswapBox.infoUntil = ImGui::GetTime() + 0.1;
            }
        }
        // Selected spike marks: Esc clears them (as does leaving choreography mode); a change says
        // what to do with them, for a few seconds.
        stageView.selectedMarks.erase(
            std::remove_if(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                           [&](int m) { return m < 0 || m >= static_cast<int>(doc.choreography.marks.size()); }),
            stageView.selectedMarks.end());
        if (!stageView.choreographyMode ||
            (!stageView.markChoice.active && !io.WantTextInput && stageView.markDrag < 0 &&
             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            stageView.selectedMarks.clear();
        if (stageView.selectedGroup >= 0 &&
            stageView.selectedMarks != doc.choreography.group(stageView.selectedGroup)) {
            std::vector<int> sorted = stageView.selectedMarks;
            std::sort(sorted.begin(), sorted.end());
            if (sorted != doc.choreography.group(stageView.selectedGroup)) stageView.selectedGroup = -1;
        }
        if (stageView.selectedMarks != stageView.selectedMarksSaid) {
            stageView.selectedMarksSaid = stageView.selectedMarks;
            if (!stageView.selectedMarks.empty()) {
                std::string list;
                for (size_t i = 0; i < stageView.selectedMarks.size(); ++i)
                    list += (i == 0 ? "" : (i + 1 == stageView.selectedMarks.size() ? " and " : ", ")) +
                            markNameOf(stageView.selectedMarks[i]);
                gridPane.siteswapBox.info =
                    std::string(stageView.selectedMarks.size() == 1 ? "Spike mark " : "Spike marks ") + list +
                    " selected: drag " + (stageView.selectedMarks.size() == 1 ? "it" : "one to move them all") +
                    " (Shift: off the grid). Shift+click adds or removes a mark; Esc clears.";
                gridPane.siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
            }
        }
        // The Walk Like... window (see WalkLikeWindow).
        ui.walkLike.previewing = false;
        if (ui.walkLike.open) {
            const JugglingLoop loop = shownLoop();
            const int j = ui.walkLike.juggler;
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
            if (loop.empty() || j < 0 || j >= loop.jugglers || cycle <= 0) {
                ui.walkLike.open = false;
            } else {
                const float em = ImGui::GetFontSize();
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                char title[48];
                std::snprintf(title, sizeof(title), "J%d Walks Like...###walk_like", j + 1);
                if (ImGui::Begin(title, &ui.walkLike.open,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                    WalkLink link = doc.choreography.link(j);
                    bool changed = false;
                    // Hovering a choice: show where it would put them.
                    auto previewIf = [&](const WalkLink& would) {
                        if (!ImGui::IsItemHovered() || would.leader < 0) return;
                        ui.walkLike.previewing = true;
                        ui.walkLike.preview = would;
                    };
                    // Leader: anyone whose walking doesn't already depend on this juggler.
                    ImGui::TextUnformatted("Leader:");
                    for (int k = 0; k < loop.jugglers; ++k) {
                        if (k == j) continue;
                        ImGui::SameLine();
                        const bool loops = doc.choreography.dependsOn(k, j);
                        ImGui::BeginDisabled(loops);
                        char label[8];
                        std::snprintf(label, sizeof(label), "J%d", k + 1);
                        if (ImGui::RadioButton(label, link.leader == k) && link.leader != k) {
                            if (!link.active() && doc.choreography.hasKeys(j)) {
                                ui.walkLike.confirmLeader = k;  // ask first: their own keyframes go
                                ui.walkLike.confirmRequested = true;
                            } else {
                                link.leader = k;
                                changed = true;
                            }
                        }
                        if (!loops) {
                            WalkLink would = link.active() ? link : WalkLink();
                            would.leader = k;
                            previewIf(would);
                        }
                        ImGui::EndDisabled();
                        if (loops && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("J%d already walks like J%d (perhaps through others).", k + 1, j + 1);
                    }
                    ImGui::BeginDisabled(!link.active());
                    // Time: J(j) on beat b does what the leader does on beat b + offset.
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Offset:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(em * 7.0f);
                    // (Offsets run over the whole choreography, rotated repeats and all.)
                    const int full = choreographyFullCycle(doc.choreography, cycle);
                    int offset = link.offset;
                    if (ImGui::InputInt("##offset", &offset, 0, 0)) {
                        link.offset = ((offset % full) + full) % full;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        if (full != cycle)
                            ImGui::SetTooltip("J%d does on each beat what the leader does this many beats later\n"
                                              "(like a siteswap link's [k]). The choreography is %d beats written out,\n"
                                              "%d in all with its walking loop, so the offset can be up to %d.",
                                              j + 1, cycle, full, full - 1);
                        else
                            ImGui::SetTooltip("J%d does on each beat what the leader does this many beats later\n"
                                              "(like a siteswap link's [k]). The choreography is %d beats long.",
                                              j + 1, cycle);
                    }
                    // - and + (Ctrl: 4 at a time), each previewing where it would put them.
                    {
                        const float button = ImGui::GetFrameHeight();
                        const int step = ImGui::GetIO().KeyCtrl ? 4 : 1;
                        for (const int sign : {-1, 1}) {
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                            const int to = ((link.offset + sign * step) % full + full) % full;
                            if (ImGui::Button(sign < 0 ? "-##offset" : "+##offset", ImVec2(button, button))) {
                                link.offset = to;
                                changed = true;
                            }
                            ImGui::PopItemFlag();
                            WalkLink would = link;
                            would.offset = to;
                            previewIf(would);
                        }
                        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                        ImGui::TextUnformatted("beats");
                    }
                    for (const int part : {2, 3, 4}) {
                        if (full % part != 0) continue;
                        ImGui::SameLine();
                        char label[16];
                        std::snprintf(label, sizeof(label), "1/%d (%d)", part, full / part);
                        if (ImGui::SmallButton(label)) {
                            link.offset = full / part;
                            changed = true;
                        }
                        WalkLink would = link;
                        would.offset = full / part;
                        previewIf(would);
                    }
                    // Past the beats written out, the leader is walking a part of the loop: say whose,
                    // and how far turned, as a check ("= J3's part 2 beats in, turned 1/4 CCW").
                    if (full != cycle && link.offset >= cycle && link.leader >= 0) {
                        const WalkLoop w = walkLoop(doc.choreography, loop.jugglers);
                        const int times = link.offset / cycle, rest = link.offset % cycle;
                        if (w.valid && times < w.phases && link.leader < static_cast<int>(w.role[static_cast<size_t>(times)].size())) {
                            const int part = w.role[static_cast<size_t>(times)][static_cast<size_t>(link.leader)];
                            const Turn turned = w.turn[static_cast<size_t>(times)][static_cast<size_t>(link.leader)];
                            const std::string who =
                                part == link.leader ? std::string("the leader") : "J" + std::to_string(part + 1) + "'s part";
                            if (turned.none())
                                ImGui::TextDisabled("  = %s %d beats later", who.c_str(), rest);
                            else
                                ImGui::TextDisabled("  = %s %d beats later, turned %s", who.c_str(), rest, turnText(turned).c_str());
                        }
                    }
                    // Turn about the middle of the floor.
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Turned:");
                    ImGui::SameLine();
                    Turn hoveredTurn;
                    hoveredTurn.den = -1;  // (none hovered)
                    if (turnEditor("##turned", &link.turn, &hoveredTurn)) changed = true;
                    if (hoveredTurn.den > 0 && link.active()) {
                        ui.walkLike.preview = link;
                        ui.walkLike.preview.turn = hoveredTurn;
                        ui.walkLike.previewing = true;
                    }
                    ImGui::TextDisabled("        about the middle of the floor, seen from above");
                    ImGui::EndDisabled();
                    if (changed && link.active()) setWalkLink(&doc.choreography, j, link);
                    ImGui::Spacing();
                    ImGui::BeginDisabled(!link.active());
                    if (ImGui::Button("Unlink", ImVec2(em * 6.0f, 0.0f))) unlinkWalk(j);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Write J%d's walking out as their own keyframes (nothing moves).", j + 1);
                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(em * 6.0f, 0.0f))) ui.walkLike.open = false;

                    // Linking a juggler who has keyframes of their own: ask first.
                    if (ui.walkLike.confirmRequested) {
                        ImGui::OpenPopup("Link this juggler?");
                        ui.walkLike.confirmRequested = false;
                    }
                    if (ImGui::BeginPopupModal("Link this juggler?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        const size_t count = j < static_cast<int>(doc.choreography.keys.size())
                                                 ? doc.choreography.keys[static_cast<size_t>(j)].size()
                                                 : 0;
                        ImGui::Text("J%d's %d keyframe%s will be deleted: J%d will walk like J%d instead.", j + 1,
                                    static_cast<int>(count), count == 1 ? "" : "s", j + 1,
                                    ui.walkLike.confirmLeader + 1);
                        ImGui::Spacing();
                        if (ImGui::Button("Link", ImVec2(em * 6.0f, 0.0f))) {
                            WalkLink l;
                            l.leader = ui.walkLike.confirmLeader;
                            setWalkLink(&doc.choreography, j, l);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                            ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                }
                ImGui::End();
            }
        }
        // The loop column's builder (see LoopBuilder).
        if (stageView.loopBuilder.requested) {
            stageView.loopBuilder.requested = false;
            ImGui::OpenPopup("Walking Loop");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
        if (ImGui::BeginPopupModal("Walking Loop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const JugglingLoop loop = shownLoop();
            const int j = stageView.loopBuilder.juggler;
            const int n = loop.empty() ? 0 : loop.jugglers;
            const int written = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
            if (j < 0 || j >= n) ImGui::CloseCurrentPopup();
            const float em = ImGui::GetFontSize();
            ImGui::Text("After beat %d, whoever's on C%d goes on to:", written, j + 1);
            for (int r = 0; r < n; ++r) {
                if (r > 0) ImGui::SameLine();
                char label[24];
                std::snprintf(label, sizeof(label), r == j ? "C%d (own)" : "C%d", r + 1);
                if (ImGui::RadioButton(label, stageView.loopBuilder.k.roleFor(j) == r))
                    stageView.loopBuilder.k.role = r == j ? -1 : r;
            }
            ImGui::TextUnformatted("...from its beat 1, turned:");
            turnEditor("##loopTurn", &stageView.loopBuilder.k.turn);
            ImGui::TextDisabled("about the middle of the floor, seen from above");
            const std::string text = continuationText(stageView.loopBuilder.k, j);
            ImGui::TextDisabled("In the cell: %s", text.empty() ? (std::to_string(j + 1) + " (its own path, unturned)").c_str()
                                                               : text.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Set", ImVec2(em * 6.0f, 0.0f))) {
                setContinuation(j, stageView.loopBuilder.k);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Renaming a spike mark (see RenameMark).
        if (ui.renameMark.requested) {
            ui.renameMark.requested = false;
            if (ui.renameMark.mark >= 0 && ui.renameMark.mark < static_cast<int>(doc.choreography.marks.size())) {
                std::snprintf(ui.renameMark.text, sizeof(ui.renameMark.text), "%s",
                              doc.choreography.marks[static_cast<size_t>(ui.renameMark.mark)].name.c_str());
                ui.renameMark.error.clear();
                ImGui::OpenPopup("Rename Spike Mark");
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            }
        }
        if (ImGui::BeginPopupModal("Rename Spike Mark", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool exists =
                ui.renameMark.mark >= 0 && ui.renameMark.mark < static_cast<int>(doc.choreography.marks.size());
            if (!exists) ImGui::CloseCurrentPopup();
            ImGui::TextUnformatted("Two characters, each 0-9 or a-z:");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            const bool entered = ImGui::InputText("##markName", ui.renameMark.text, sizeof(ui.renameMark.text),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
            // Only 0-9 and a-z (capitals made small).
            {
                std::string kept;
                for (const char* ch = ui.renameMark.text; *ch; ++ch) {
                    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(*ch)));
                    if ((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'z')) kept += lower;
                }
                std::snprintf(ui.renameMark.text, sizeof(ui.renameMark.text), "%s", kept.c_str());
            }
            if (!ui.renameMark.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", ui.renameMark.error.c_str());
            const float em = ImGui::GetFontSize();
            if ((ImGui::Button("Rename", ImVec2(em * 6.0f, 0.0f)) || entered) && exists) {
                const std::string name = ui.renameMark.text;
                const int other = findMarkNamed(doc.choreography, name);
                if (!validMarkName(name)) {
                    ui.renameMark.error = "It needs two characters.";
                    sound.plipWanted = true;
                } else if (other >= 0 && other != ui.renameMark.mark) {
                    ui.renameMark.error = "Another mark is called " + name + ".";
                    sound.plipWanted = true;
                } else {
                    doc.choreography.marks[static_cast<size_t>(ui.renameMark.mark)].name = name;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // The Walk Like... window's preview: where the choice under the mouse would put them, now.
        WalkGhost walkGhost;
        if (ui.walkLike.open && ui.walkLike.previewing) {
            const JugglingLoop loop = shownLoop();
            const int j = ui.walkLike.juggler;
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
            if (cycle > 0 && j >= 0 && j < static_cast<int>(scene.jugglers.size()) && j < loop.jugglers) {
                Choreography trial = doc.choreography;
                setWalkLink(&trial, j, ui.walkLike.preview);
                JuggleParams params = doc.juggleParams;
                params.choreography = &trial;
                Vec3 at;
                float yaw = 0.0f;
                jugglerPlacementAt(loop, params, j, playback.beat, &at, &yaw);
                walkGhost.show = true;
                walkGhost.pose = posedJuggler(scene.jugglers[static_cast<size_t>(j)], scene.prop);
                walkGhost.placement = Mat4::translation(at) * Mat4::rotationY(yaw);
                const ChoreographyPaths trialPaths = choreographyPaths(trial, loop.jugglers, cycle, choreographyScale(loop));
                if (j < static_cast<int>(trialPaths.lines.size())) walkGhost.path = trialPaths.lines[static_cast<size_t>(j)];
            }
        }
        // The loiter popup (see LoiterPopup).
        {
            const JugglingLoop loop = shownLoop();
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
            if (ui.loiter.openRequested) {
                ui.loiter.openRequested = false;
                if (cycle > 0) ImGui::OpenPopup("##loiter");
            }
            ImGui::SetNextWindowPos(ImVec2(ui.loiter.at.x + 16.0f, ui.loiter.at.y + 16.0f), ImGuiCond_Appearing);
            const bool flashing = ImGui::GetTime() < ui.loiter.errorUntil;
            const ImU32 errorColor = errorTextColor(settings.colorVision);
            if (flashing) {
                ImGui::PushStyleColor(ImGuiCol_Border, errorColor);
                ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 3.0f);
            }
            ui.loiter.open = ImGui::BeginPopup("##loiter");
            if (flashing) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            if (ui.loiter.open) {
                const int j = ui.loiter.juggler;
                const int b = ui.loiter.arrival.beat;
                // Whether a loiter of n beats fits: it mustn't reach (or pass) another of this
                // juggler's keyframes, and must be shorter than the choreography.
                auto fits = [&](int n, int* blockingBeat) {
                    if (n == 0) return true;
                    if (n >= cycle) {
                        if (blockingBeat) *blockingBeat = -1;
                        return false;
                    }
                    // With rotated repeats a loiter can't run on past the beats written out: the
                    // next beats are the first ones turned.
                    if (doc.choreography.repeats() > 1 && b + n >= cycle) {
                        if (blockingBeat) *blockingBeat = -2;
                        return false;
                    }
                    if (j >= 0 && j < static_cast<int>(doc.choreography.keys.size()))
                        for (const Keyframe& k : doc.choreography.keys[static_cast<size_t>(j)]) {
                            const int d = ((k.beat - b) % cycle + cycle) % cycle;
                            if (k.beat != b && d >= 1 && d <= n) {
                                if (blockingBeat) *blockingBeat = d;
                                return false;
                            }
                        }
                    return true;
                };
                auto apply = [&](int n) {
                    int blocking = 0;
                    if (!fits(n, &blocking)) {
                        ui.loiter.error = blocking == -2 ? "Too long: it would run past beat " + std::to_string(cycle) +
                                                            ", into the walking loop's copies."
                                       : blocking < 0 ? "Too long: the choreography is only " + std::to_string(cycle) + " beats."
                                                    : "No room: J" + std::to_string(j + 1) + " has a keyframe " +
                                                          std::to_string(blocking) + " beats later.";
                        ui.loiter.errorUntil = ImGui::GetTime() + 1.5;
                        return;
                    }
                    if (n > 0) {
                        Keyframe stay = ui.loiter.arrival;
                        stay.beat = (b + n) % cycle;
                        stay.longTurn = false;
                        setKeyframe(&doc.choreography, j, stay);
                    }
                    ImGui::CloseCurrentPopup();
                };
                // Loiter lengths already in the choreography, nearest in time to this beat first
                // (beyond the 0-4 buttons); up to four, shortest first.
                std::vector<std::pair<int, int>> used;  // (distance in time, length)
                for (size_t jj = 0; jj < doc.choreography.keys.size(); ++jj) {
                    const std::vector<Keyframe>& keys = doc.choreography.keys[jj];
                    for (size_t i = 0; i < keys.size() && keys.size() > 1; ++i) {
                        const Keyframe& k = keys[i];
                        const Keyframe& next = keys[(i + 1) % keys.size()];
                        const int gap = ((next.beat - k.beat) % cycle + cycle) % cycle;
                        if (gap <= 4 || !sameKeyframeSpot(doc.choreography, k, next)) continue;
                        if (static_cast<int>(jj) == j && k.beat == b) continue;  // (this one, if it's already a loiter)
                        const int d = ((k.beat - b) % cycle + cycle) % cycle;
                        used.emplace_back(std::min(d, cycle - d), gap);
                    }
                }
                std::sort(used.begin(), used.end());
                std::vector<int> recent;
                for (const std::pair<int, int>& u : used)
                    if (recent.size() < 4 && std::find(recent.begin(), recent.end(), u.second) == recent.end())
                        recent.push_back(u.second);
                std::sort(recent.begin(), recent.end());

                const std::string where =
                    ui.loiter.arrival.mark >= 0 ? "on mark " + markNameOf(ui.loiter.arrival.mark) : "here";
                ImGui::Text("J%d arrives on beat %ld. Loiter %s for how many beats?", j + 1, ui.loiter.onBeat + 1,
                            where.c_str());
                const float em = ImGui::GetFontSize();
                auto choice = [&](int n) {
                    ImGui::PushID(n);
                    const bool ok = fits(n, nullptr);
                    ImGui::BeginDisabled(!ok);
                    if (ImGui::Button(std::to_string(n).c_str(), ImVec2(em * 2.2f, 0.0f))) apply(n);
                    ImGui::EndDisabled();
                    if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Doesn't fit: it would reach another of J%d's keyframes.", j + 1);
                    ImGui::PopID();
                    ImGui::SameLine();
                };
                for (int n = 0; n <= 4; ++n) choice(n);
                if (!recent.empty()) {
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    for (const int n : recent) choice(n);
                }
                ImGui::NewLine();
                ImGui::SetNextItemWidth(em * 5.0f);
                const bool entered = ImGui::InputText("beats", ui.loiter.text, sizeof(ui.loiter.text),
                                                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
                const bool typing = ImGui::IsItemActive();
                ImGui::SameLine();
                if ((ImGui::Button("OK") || entered) && ui.loiter.text[0] != '\0') apply(std::atoi(ui.loiter.text));
                ImGui::TextDisabled("Or press a key: 0-9, a = 10, b = 11 ... (as in siteswap). Esc: just arrive.");
                if (ImGui::GetTime() < ui.loiter.errorUntil)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorColor), "%s", ui.loiter.error.c_str());
                if (!typing) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
                    for (const ImWchar c : io.InputQueueCharacters) {
                        int n = -1;
                        if (c >= '0' && c <= '9') n = c - '0';
                        else if (c >= 'a' && c <= 'z') n = 10 + (c - 'a');
                        else if (c >= 'A' && c <= 'Z') n = 10 + (c - 'A');
                        if (n >= 0) {
                            apply(n);
                            break;
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }
        // Help > 3D Mouse Diagnostics: what the cap is sending, live (-1..1 on each axis).
        if (ui.showSpaceMouseDiagnostics) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("3D Mouse Diagnostics", &ui.showSpaceMouseDiagnostics,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                if (!spaceMouse.present) {
                    ImGui::TextUnformatted("No 3D mouse found.");
                } else {
                    ImGui::TextUnformatted("Move the cap: what JuggleSim reads, -1 to 1 on each axis.");
                    ImGui::Text("Slide   right %+.2f   away %+.2f   up %+.2f", spaceMouse.move[0], spaceMouse.move[1],
                                spaceMouse.move[2]);
                    ImGui::Text("Turn    tilt %+.2f   roll %+.2f   spin %+.2f", spaceMouse.turn[0], spaceMouse.turn[1],
                                spaceMouse.turn[2]);
                    const bool idle = std::fabs(spaceMouse.turn[0]) < 0.06f && std::fabs(spaceMouse.move[1]) < 0.06f;
                    const bool tiltWins = std::fabs(spaceMouse.turn[0]) > 0.8f * std::fabs(spaceMouse.move[1]);
                    ImGui::Text("Tilt vs push/pull: %s", idle ? "-" : (tiltWins ? "tilt (turns the stage)" : "push/pull (moves)"));
                    ImGui::Text(
                        "Mouse-wheel events from the driver: %s",
                        ImGui::GetTime() - stageView.spaceMouseWheelBlocked < 1.0 ? "yes (ignored)" : "none seen");
                }
            }
            ImGui::End();
        }

        // File > New Pattern..., first offering to save the current pattern if it has changed.
        if (ui.savePromptRequested) {
            ImGui::OpenPopup("Save changes?");
            ui.savePromptRequested = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            ImGui::PushTextWrapPos(em * 34.0f);
            ImGui::Text("Save %s%s to My Patterns before %s?",
                        openPatternIndex() >= 0 ? ("\"" + doc.openPatternName + "\"").c_str() : doc.siteswapText,
                        doc.savedChoreography != choreographyText() ? " (and its choreography)"
                        : doc.savedThrowNext != throwNextText(doc.throwNext) ? " (and its throwing loop)" : "",
                        ui.promptForQuit ? "quitting" : "starting a new pattern");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const bool open = openPatternIndex() >= 0;
            if (ImGui::Button(open ? "Save" : "Save...", ImVec2(em * 7.0f, 0.0f))) {
                if (open) {
                    // Over the open pattern, then on.
                    storeMyPattern(doc.openPatternName, true);
                    if (ui.promptForQuit)
                        ui.done = true;
                    else
                        ui.newPatternDialog.openRequested = true;
                } else {
                    std::snprintf(library.saveDialog.name, sizeof(library.saveDialog.name), "%s", doc.siteswapText);
                    library.saveDialog.openRequested = true;
                    (ui.promptForQuit ? ui.quitAfterSave : ui.newAfterSave) = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(em * 7.0f, 0.0f))) {
                if (ui.promptForQuit)
                    ui.done = true;
                else
                    ui.newPatternDialog.openRequested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ui.newPatternDialog.openRequested) {
            ImGui::OpenPopup("New Pattern");
            ui.newPatternDialog.openRequested = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("New Pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            ImGui::PushTextWrapPos(em * 26.0f);
            ImGui::TextUnformatted("Starts a blank pattern: every throw is blank (not decided yet). Type throws "
                                   "into the grid, or on the ladder click a ? to draw a throw from it, then where "
                                   "it's caught, and so on.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Jugglers");
            ImGui::SameLine(em * 5.0f);
            for (int n = 1; n <= kMaxJugglers; ++n) {
                if (n > 1) ImGui::SameLine();
                char label[8];
                std::snprintf(label, sizeof(label), "%d", n);
                ImGui::RadioButton(label, &ui.newPatternDialog.jugglers, n);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Period");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            ImGui::InputInt("##period", &ui.newPatternDialog.period);
            ui.newPatternDialog.period = std::clamp(ui.newPatternDialog.period, 1, kMaxLoopBeats);
            ImGui::SameLine();
            ImGui::TextDisabled("beats");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Count");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            const std::string countName = ui.newPatternDialog.count >= 2
                                              ? std::to_string(ui.newPatternDialog.count) + "-count"
                                              : std::string("None");
            if (ImGui::BeginCombo("##count", countName.c_str())) {
                if (ImGui::Selectable("None", ui.newPatternDialog.count < 2)) ui.newPatternDialog.count = 0;
                for (int n = 2; n <= 8; ++n)
                    if (ImGui::Selectable((std::to_string(n) + "-count").c_str(), ui.newPatternDialog.count == n))
                        ui.newPatternDialog.count = n;
                ImGui::EndCombo();
            }
            if (ui.newPatternDialog.count >= 2) {
                ImGui::SameLine();
                ImGui::TextDisabled("(the period becomes a multiple of %d)", ui.newPatternDialog.count);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Walking");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            ImGui::InputInt("##walking", &ui.newPatternDialog.walking);
            ui.newPatternDialog.walking = std::clamp(ui.newPatternDialog.walking, 0, kMaxChoreographyLength);
            ImGui::SameLine();
            ImGui::TextDisabled(ui.newPatternDialog.walking == 0 ? "beats (0: no choreography yet)" : "beats");
            if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("The choreography's own period: the beats of walking written out before its\n"
                                  "loop (a short passing pattern can have a long walk, and the other way round).");
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(em * 6.0f, 0.0f))) {
                JugglingLoop blank;
                const int n = ui.newPatternDialog.count;
                blank.jugglers = ui.newPatternDialog.jugglers;
                blank.period = n >= 2 ? (ui.newPatternDialog.period + n - 1) / n * n : ui.newPatternDialog.period;
                for (int j = 0; j < blank.jugglers; ++j)
                    for (int b = 0; b < blank.period; ++b)
                        blank.throws.push_back({n >= 2 && b % n != 0 ? 3 : kOpenThrow, j});
                std::string text;
                if (loopToText(blank, &text)) {
                    commitTyping();
                    playback.beat = 0.0;
                    recordSettingsChange();
                    const PatternSettings before = currentPatternSettings();
                    showPatternText(text);
                    doc.choreography = Choreography();  // a new pattern starts in the default formation
                    if (ui.newPatternDialog.walking > 0) {
                        // ...with spike marks under everyone, and the walking's own period.
                        // (A length of its own counts as a change: an untouched choreography follows the
                        // siteswap's period, which would put this one back to it.)
                        ensureSpikeMarks();
                        doc.choreography.cycle = ui.newPatternDialog.walking;
                        stageView.choreographyMode = true;
                    }
                    stageView.camera.free = false;
                    doc.patternCount = n >= 2 ? n : 0;
                    doc.throwNext.clear();
                    doc.openPatternName.clear();
                    doc.history.recordWithSettings(text, before, currentPatternSettings());
                    settingsRecorded();
                    markSaved();
                    stageView.selectedJuggler = -1;
                    stageView.frameSelected = false;
                    resetLadderView(ladderPane.ladderEdit);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        pollSettingsChange();

        // The camera (after this frame's orbit and zoom), and the juggler numbers, drawn by ImGui
        // behind its windows but over the 3D view.
        const float jugglerAspect = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
        // The top view frames the whole floor in use: the jugglers and the spike marks. (Not
        // while a mark is being dragged: the view would shift under the mouse.)
        // While a juggler or mark is being moved (or the loiter popup is up), it doesn't shrink,
        // and grows only slowly (pushing something against the edge widens the view gently), so
        // the floor doesn't shift under the mouse.
        {
            float half = 1.5f;
            for (const JugglerState& js : scene.jugglers) half = std::max(half, std::max(std::fabs(js.position.x), std::fabs(js.position.z)) + 0.8f);
            const float markScale = choreographyScale(loop);
            for (const SpikeMark& m : doc.choreography.marks)
                half = std::max(half, std::max(std::fabs(m.x), std::fabs(m.z)) * markScale + 0.8f);
            const bool placing = stageView.jugglerDrag >= 0 || stageView.markDrag >= 0 || ui.loiter.open;
            if (!placing)
                stageView.camera.floorHalf = half;
            else if (half > stageView.camera.floorHalf)
                stageView.camera.floorHalf +=
                    std::min(half - stageView.camera.floorHalf, 0.5f * io.DeltaTime);  // 0.5 m a second
        }
        // The 3D mouse, grabbing the stage: spin and tilt the cap to orbit; push it away to move
        // back (zoom out), pull it toward you to move in; slide it sideways or up and down and
        // the stage goes with it (which frees the camera). Each axis has a small dead zone and
        // rises gently, so small movements are fine.
        if (spaceMouse.present) {
            auto shaped = [](float v) {
                const float deadZone = 0.06f;
                const float a = std::fabs(v);
                if (a <= deadZone) return 0.0f;
                const float t = (a - deadZone) / (1.0f - deadZone);
                return std::copysign(t * t, v);
            };
            const float step = io.DeltaTime * settings.spaceMouseSpeed;
            float slideX = shaped(spaceMouse.move[0]), slideY = shaped(spaceMouse.move[1]), slideZ = shaped(spaceMouse.move[2]);
            const float spin = shaped(spaceMouse.turn[2]);
            float tilt = shaped(spaceMouse.turn[0]);
            // Tilting the cap toward or away from you pushes or pulls it too (often 60-100% as
            // far), and a push tilts it a little: the larger of the two (as read) is meant, and
            // the other is ignored.
            if (std::fabs(spaceMouse.turn[0]) > 0.8f * std::fabs(spaceMouse.move[1]))
                slideY = 0.0f;
            else
                tilt = 0.0f;
            // Turning the cap nudges it sideways too: while it's mostly being turned, its slides
            // are ignored (or a free camera, which moves faster the further away it is, can run
            // off).
            const float turnAmount = std::max(std::fabs(spin), std::fabs(tilt));
            const float slideAmount = std::max(std::fabs(slideX), std::max(std::fabs(slideY), std::fabs(slideZ)));
            if (turnAmount > 2.0f * slideAmount) slideX = slideY = slideZ = 0.0f;
            const bool turning = spin != 0.0f || tilt != 0.0f;
            const bool sliding = slideX != 0.0f || slideY != 0.0f || slideZ != 0.0f;
            // Anything done with the 3D mouse makes the camera free, from where it is.
            if (!stageView.camera.free && (turning || sliding)) {
                makeCameraFree(stageView.camera, stageView.lastCamera);
                stageView.spaceOrbitUntil = 0.0;
            }
            if (stageView.camera.free) {
                const double now = ImGui::GetTime();
                if (spin != 0.0f || tilt != 0.0f) {
                    // Spinning and tilting turn the stage like an object in your hand: the
                    // camera orbits round a center that stays put while the cap keeps turning
                    // (and a moment after), round the vertical for a spin and over the top for
                    // a tilt (never past straight down or up, so it never turns upside down).
                    if (now >= stageView.spaceOrbitUntil)
                        stageView.orbitCenter =
                            freeOrbitCenter(stageView.lastCamera, scene, stageView.selectedJuggler, stageBox());
                    stageView.spaceOrbitUntil = now + 0.3;
                    orbitFreeCamera(stageView.camera, stageView.orbitCenter, -spin * 2.5f * step, tilt * 1.5f * step);
                }
                if (sliding) {
                    Vec3 forward, right, up;
                    freeCameraAxes(stageView.camera, &forward, &right, &up);
                    const float v = 1.5f * nearestToCamera(stageView.camera.freePos) * step;
                    stageView.camera.freePos =
                        stageView.camera.freePos - right * (slideX * v) - up * (slideZ * v) - forward * (slideY * v);
                }
            }
        }
        const CameraView cameraView = updateCamera(stageView.camera, stage.extents, jugglerAspect, io.DeltaTime);
        stageView.lastViewProj = cameraViewProj(cameraView, jugglerAspect);
        stageView.lastCamera = cameraView;
        stageView.lastAspect = jugglerAspect;
        // Floor overlays: their lines go in the 3D pass (overlayTriangles, drawn after the scene,
        // so bodies hide them); their text, here, where no body is in front of it.
        stageView.overlayTriangles.clear();
        {
            OverlayBuilder overlay(&stageView.overlayTriangles, cameraView, viewSize.y);
            const BodyOccluder bodies(scene, cameraView.position);
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            if (stageView.choreographyMode && doc.choreography.active()) {
                const StageBox box = stageBox();
                buildFloorGrid(overlay, settings, box.min.x, box.min.z, box.max.x, box.max.z);
                std::vector<int> lit = stageView.markChoice.active ? stageView.markChoice.marks : std::vector<int>();
                lit.insert(lit.end(), stageView.selectedMarks.begin(), stageView.selectedMarks.end());
                buildSpikeMarks(overlay, doc.choreography, choreographyScale(loop), stageView.markDrag, lit);
                drawSpikeMarkNumbers(bg, doc.choreography, choreographyScale(loop), stageView.lastViewProj, viewMin,
                                     viewSize, bodies, lit, markGroupView());
            }
            if (showPaths) {
                const int pathSelected = loop.jugglers == 1 ? 0 : stageView.selectedJuggler;
                buildChoreographyPaths(overlay, paths, pathSelected, hoveredPathPoint);
                drawChoreographyPathLabels(bg, paths, pathCycle, stageView.lastViewProj, viewMin, viewSize,
                                           pathSelected, hoveredPathPoint, settings.pathBeatNumbers, bodies);
            }
            // The reticle: what the camera orbits about. Always shown (faintly) for a free
            // camera, brighter while orbiting; for the automatic framing, only while orbiting.
            const bool orbitingNow = stageView.cameraOrbiting || ImGui::GetTime() < stageView.spaceOrbitUntil;
            if (stageView.camera.free || orbitingNow) {
                const Vec3 center =
                    !stageView.camera.free
                        ? cameraView.target
                        : (orbitingNow ? stageView.orbitCenter
                                       : freeOrbitCenter(cameraView, scene, stageView.selectedJuggler, stageBox()));
                buildOrbitReticle(overlay, center, orbitingNow);
            }
        }
        // The walking loop doesn't work: the stage is as it last was that did, tinted red round the edge.
        if (stage.walkLoopBroken || stage.throwLoopBroken || stage.throwsBroken) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            const ImVec2 b(viewMin.x + viewSize.x, viewMin.y + viewSize.y);
            bg->AddRectFilled(viewMin, b, IM_COL32(200, 40, 30, 22));
            for (int i = 0; i < 6; ++i) {
                const float inset = static_cast<float>(i) * 3.0f;
                bg->AddRect(ImVec2(viewMin.x + inset, viewMin.y + inset), ImVec2(b.x - inset, b.y - inset),
                            IM_COL32(220, 50, 40, 150 - i * 24), 0.0f, 0, 3.0f);
            }
            const char* says =
                stage.throwsBroken ? "The throws have an error (see the grid): this is the pattern as it last worked"
                : stage.throwLoopBroken ? (stage.walkLoopBroken ? "Walking and throwing loops: see the grid"
                                                                : "Throwing loop: see the grid")
                                        : "Walking loop: see the grid";
            bg->AddText(ImVec2(viewMin.x + 24.0f, b.y - ImGui::GetTextLineHeight() - 20.0f), IM_COL32(255, 140, 120, 255), says);
        }
        drawJugglerLabels(ImGui::GetBackgroundDrawList(), scene, stageView.lastViewProj, viewMin, viewSize,
                          stageView.selectedJuggler, &stageView.jugglerLabelCenters);
        if (settings.showThrowValues)
            drawPropValueLabels(ImGui::GetBackgroundDrawList(), scene, stageView.lastViewProj, viewMin, viewSize,
                                settings.colorVision, styleOfBall, doc.parsed.form.relative);
        // The pattern's name, if it has one, in the top-left corner of the juggler pane.
        if (doc.patternValid) {
            const std::map<std::string, std::string>::const_iterator named =
                library.patternNames.find(canonicalSiteswap(doc.pattern));
            if (named != library.patternNames.end()) {
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                const ImVec2 at(viewMin.x + panelMargin * 1.5f, viewMin.y + panelMargin);
                const char* name = named->second.c_str();
                bg->AddText(gfx.titleFont, kTitleFontSize, ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 160),
                            name);
                bg->AddText(gfx.titleFont, kTitleFontSize, at, IM_COL32(232, 234, 240, 255), name);
            }
        }
        // A free camera stays where it's put; say so (and how to get the automatic framing back).
        if (stageView.camera.free) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            const char* note = "Free camera: Home re-frames";
            const ImVec2 size = ImGui::CalcTextSize(note);
            const ImVec2 at(viewMin.x + viewSize.x - size.x - panelMargin * 1.5f, viewMin.y + viewSize.y - size.y - panelMargin);
            bg->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 160), note);
            bg->AddText(at, IM_COL32(200, 204, 214, 220), note);
        }

        // The window's title: the open pattern (its name if it's one of mine, else its
        // siteswap), with a * while it has changes that aren't saved.
        {
            const bool unsaved =
                (doc.patternValid || doc.sketching) &&
                (doc.savedPatternText != doc.siteswapText || doc.savedChoreography != choreographyText() ||
                 doc.savedThrowNext != throwNextText(doc.throwNext));
            const std::string shown = openPatternIndex() >= 0 ? doc.openPatternName : std::string(doc.siteswapText);
            const std::string title =
                shown.empty() ? std::string("JuggleSim") : shown + (unsaved ? "*" : "") + " \xe2\x80\x94 JuggleSim";
            if (title != ui.windowTitle) {
                ui.windowTitle = title;
                platformSetWindowTitle(title.c_str());
            }
        }

        // The plip: something the user just did was refused (a note under the siteswap grid, or
        // the loiter popup's) or broke the pattern. Once per thing, however it was refused.
        if (gridPane.siteswapBox.noteUntil != sound.lastNoteUntil) {
            if (gridPane.siteswapBox.noteUntil > sound.lastNoteUntil && !gridPane.siteswapBox.note.empty())
                sound.plipWanted = true;
            sound.lastNoteUntil = gridPane.siteswapBox.noteUntil;
        }
        if (ui.loiter.errorUntil != ui.lastLoiterErrorUntil) {
            if (ui.loiter.errorUntil > ui.lastLoiterErrorUntil) sound.plipWanted = true;
            ui.lastLoiterErrorUntil = ui.loiter.errorUntil;
        }
        // (One sound at a time: the plip wins over a tick.)
        if (sound.plipWanted && settings.sounds) {
            const std::vector<unsigned char>& wav = plipWav();
            platformPlaySound(wav.data(), wav.size());
        } else if (sound.tickWanted > 0) {
            const std::vector<unsigned char>& wav = tickWav(sound.tickWanted == 2);
            platformPlaySound(wav.data(), wav.size());
        }
        sound.plipWanted = false;
        sound.tickWanted = 0;

        ImGui::Render();

        // --- Draw ---
        // OpenGL works in pixels and ImGui in points; on a high-DPI (Retina) display a point is
        // several pixels.
        int framebufferWidth = 0, framebufferHeight = 0;
        platformFramebufferSize(&framebufferWidth, &framebufferHeight);
        const float pixelsPerPointX = io.DisplaySize.x > 0.0f ? framebufferWidth / io.DisplaySize.x : 1.0f;
        const float pixelsPerPointY = io.DisplaySize.y > 0.0f ? framebufferHeight / io.DisplaySize.y : 1.0f;
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Right half, between the menu bar and the transport bar (above the siteswap entry).
        // GL's origin is bottom-left.
        const int viewX = static_cast<int>(std::lround(leftWidth * pixelsPerPointX));
        const int bottomPixels = static_cast<int>(std::lround((H - bodyBottom + transportHeight) * pixelsPerPointY));
        const int topPixels = static_cast<int>(std::lround(top * pixelsPerPointY));
        const GLRect jugglerRect{viewX, bottomPixels, framebufferWidth - viewX,
                                 framebufferHeight - topPixels - bottomPixels};
        // (Torches light the stage, except while working on the choreography, which wants the
        // ordinary light to see the floor and its marks by.)
        renderJugglerView(gfx.renderer, gfx.prims, gfx.propMeshes, scene, cameraView, stageView.selectedJuggler,
                          settings.colorVision, styleOfBall, jugglerRect,
                          scene.prop == PropType::Torch && !stageView.choreographyMode);
        if (jugglerRect.w > 0 && jugglerRect.h > 0) {
            glViewport(jugglerRect.x, jugglerRect.y, jugglerRect.w, jugglerRect.h);
            gfx.renderer.drawOverlay(
                cameraViewProj(cameraView, static_cast<float>(jugglerRect.w) / static_cast<float>(jugglerRect.h)),
                stageView.overlayTriangles);
            renderWalkGhost(gfx.renderer, gfx.prims, walkGhost, cameraView, jugglerRect, ImGui::GetTime());
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        platformSwapBuffers();
    }

    gfx.propMeshes.destroy();
    gfx.prims.destroy();
    gfx.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    return 0;
}
