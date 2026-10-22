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
#include "document.h"
#include "grid_pane.h"
#include "ladder_pane.h"
#include "stage_pane.h"
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
    doc.history.current = doc.siteswapText;

    doc.juggleParams.prop = settings.prop;
    doc.juggleParams.choreography = &doc.choreography;  // (the stage's evaluation uses stageParams, below)
    auto choreographyText = [&]() {
        return doc.choreography.active() ? choreographyToText(doc.choreography) : std::string();
    };
                                    // (not for selecting a juggler standing on it)
    // The loop the jugglers juggle (the sketch, or the pattern; empty if neither).
    auto shownLoop = [&]() -> JugglingLoop {
        if (doc.sketching) return doc.sketchLoop;
        if (doc.patternValid) return patternLoop(doc.pattern);
        // (The throws have an error: the pattern as it last worked, while it has the same jugglers.
        // One left from an earlier pattern isn't this one.)
        if (stage.haveLastGood && stage.lastGoodDerived.written.jugglers == doc.parsed.loop.jugglers)
            return stage.lastGoodDerived.written;
        return JugglingLoop();
    };
    // A spike mark's name ("0a"), or "?" for none.
    auto markNameOf = [&](int m) {
        return m >= 0 && m < static_cast<int>(doc.choreography.marks.size())
                   ? doc.choreography.marks[static_cast<size_t>(m)].name
                   : std::string("?");
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
    // The pattern being edited, as one value (see document.h).
    auto currentDocument = [&]() -> Document {
        Document d;
        d.siteswap = doc.siteswapText;
        d.count = doc.patternCount;
        d.throwNext = doc.throwNext;
        if (doc.choreography.active()) d.choreography = doc.choreography;
        d.prop = doc.juggleParams.prop;
        d.tempo = doc.juggleParams.bpm;
        d.dwell = doc.juggleParams.dwellBeats;
        d.distance = doc.juggleParams.distance;
        const PatternSettings s = settingsForSaving();
        d.hasCamera = s.hasCamera;
        if (s.hasCamera)
            for (int i = 0; i < 5; ++i) d.camera[i] = s.camera[i];
        return d;
    };
    // What's showing is what's saved (just loaded, saved or started).
    auto markSaved = [&]() { doc.saved = currentDocument(); };
    // Whether there's a pattern with changes since (so File > New Pattern... and quitting ask first).
    auto hasUnsavedChanges = [&]() { return (doc.patternValid || doc.sketching) && currentDocument() != doc.saved; };
    // The document worked out (see deriveDocument), again only if what it derives from has changed.
    auto refreshDerived = [&]() {
        const Document now = currentDocument();
        if (!stage.derivedValid || !now.derivesSameAs(stage.derivedFor)) {
            stage.derived = deriveDocument(now);
            stage.derivedValid = true;
        }
        stage.derivedFor = now;
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
        if (hasUnsavedChanges()) {
            ui.promptForQuit = true;
            ui.savePromptRequested = true;
        } else {
            ui.done = true;
        }
    };
    // Makes the changes asked for in the 3D view (see drawStagePane). `loop`: the loop shown this
    // frame.
    auto applyStageRequest = [&](const StagePaneRequest& r, const JugglingLoop& loop) {
        if (r.keyframeJuggler >= 0) setKeyframe(&doc.choreography, r.keyframeJuggler, r.keyframe);
        if (r.loiterJuggler >= 0) openLoiterPopup(r.loiterJuggler, r.loiterArrival, r.loiterAt);
        // Dropped after a drag: how long do they stay? (Or, dropped where several marks are, which
        // one first.)
        if (r.droppedJuggler >= 0 && r.droppedJuggler < loop.jugglers && !loop.empty()) {
            const int cycle = std::max(1, choreographyCycle(loop, doc.juggleParams));
            const long beat = std::lround(playback.beat);
            const int b = static_cast<int>(((beat % cycle) + cycle) % cycle);
            Keyframe placed;
            bool found = false;
            if (r.droppedJuggler < static_cast<int>(doc.choreography.keys.size()))
                for (const Keyframe& k : doc.choreography.keys[static_cast<size_t>(r.droppedJuggler)])
                    if (k.beat == b) {
                        placed = k;
                        found = true;
                    }
            // Dropped off every mark: a new mark there, which the keyframe is on (every keyframe is
            // on a mark).
            if (found && placed.mark < 0) {
                placed.mark = markForSpot(&doc.choreography, placed.x, placed.z, placed.yaw);
                setKeyframe(&doc.choreography, r.droppedJuggler, placed);
            }
            const std::vector<int> here =
                found && placed.mark >= 0 && r.dropOnFloor
                    ? marksAround(doc.choreography, r.dropFloor.x, r.dropFloor.z, choreographyScale(doc, loop))
                    : std::vector<int>();
            if (here.size() >= 2) {
                stageView.markChoice = MarkChoice{true, r.droppedJuggler, placed, here, r.dropAt};
            } else if (found) {
                openLoiterPopup(r.droppedJuggler, placed, r.dropAt);
            }
        }
        for (const std::pair<int, SpikeMark>& moved : r.movedMarks)
            if (moved.first >= 0 && moved.first < static_cast<int>(doc.choreography.marks.size()))
                doc.choreography.marks[static_cast<size_t>(moved.first)] = moved.second;
        if (r.renameMark >= 0) {
            ui.renameMark.mark = r.renameMark;
            ui.renameMark.requested = true;
        }
        if (r.deleteMark >= 0) {
            std::string why;
            if (!removeSpikeMark(&doc.choreography, r.deleteMark, loop.empty() ? 0 : loop.jugglers, &why)) {
                gridPane.siteswapBox.note = why;
                gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            } else {
                stageView.selectedMarks.clear();
            }
        }
        if (r.addMark) {
            const int added = addSpikeMark(&doc.choreography, r.newMark);
            if (added >= 0) {  // (selected, to see which it is and turn it)
                stageView.selectedMarks.assign(1, added);
                stageView.selectedJuggler = -1;
            }
        }
        if (r.walkLikeJuggler >= 0) openWalkLike(r.walkLikeJuggler);
        if (r.pause) playback.playing = false;
        if (r.goToBeat) playback.beat = r.beat;
        if (!r.note.empty()) {
            gridPane.siteswapBox.note = r.note;
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + r.noteSeconds;
        }
    };
    // Makes the changes asked for in the ladder pane (see drawLadderPane). `writtenLoop`: the throws
    // as written this frame.
    auto applyLadderRequest = [&](const LadderPaneRequest& r, const JugglingLoop& writtenLoop) {
        LadderEditResult edited = r.edited;
        if (r.toolbar.toggleOrbits) {
            settings.colorByOrbit = !settings.colorByOrbit;
            saveSettings(settings);
        }
        if (r.toolbar.toggleDimLinked) {
            settings.dimLinked = !settings.dimLinked;
            saveSettings(settings);
        }
        if (r.toolbar.toggleValues) {
            settings.showThrowValues = !settings.showThrowValues;
            saveSettings(settings);
        }
        if (r.toolbar.newPeriodBeats > 0 && doc.patternCount >= 2 && r.toolbar.newPeriodBeats % doc.patternCount != 0) {
            gridPane.siteswapBox.note = "Can't: this is a " + std::to_string(doc.patternCount) +
                                        "-count, so the period goes up and down " +
                                        std::to_string(doc.patternCount) + " beats at a time.";
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
        } else if (r.toolbar.newPeriodBeats > 0) {
            cancelLadderEdit(ladderPane.ladderEdit);
            commitTyping();
            const JugglingLoop longer = loopWithPeriod(writtenLoop, r.toolbar.newPeriodBeats);
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
        // Picking up a throw pauses the jugglers (don't confuse them mid-edit); drawing in a
        // sketch doesn't, so you can watch props pop in as you go.
        if (edited.startedChain) playback.playing = false;
        if (!edited.note.empty()) {
            gridPane.siteswapBox.note = edited.note;
            gridPane.siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
        }
        if (edited.selectJuggler != kNoSelectionChange) {
            stageView.selectedJuggler = edited.selectJuggler;
            if (stageView.selectedJuggler < 0) stageView.frameSelected = false;
        }
        if (edited.committed) {
            const bool wasWorking = doc.parsed.valid || doc.parsed.sketch;
            // An edit to the loop played out goes into the beats written out (if that's where it was).
            JugglingLoop changed = edited.loop;
            std::string why;
            if (stage.derived.played.expanded &&
                !writtenFromPlayed(stage.derived.played, writtenLoop, edited.loop, stage.derived.paths, &changed, &why)) {
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
    };
    // Makes the changes asked for in the siteswap grid (see drawGridPane). `writtenLoop`: the
    // throws as written this frame.
    auto applyGridRequest = [&](const SiteswapGridResult& g, const JugglingLoop& writtenLoop) {
        // The throws' next column: whose throws a juggler takes up after the period. (A change
        // that leaves it not working plips.)
        for (const std::pair<int, int>& e : g.throwNextEdits) {
            if (e.first < 0 || e.first >= kMaxJugglers) continue;
            if (static_cast<int>(doc.throwNext.size()) <= e.first)
                doc.throwNext.resize(static_cast<size_t>(e.first) + 1, -1);
            doc.throwNext[static_cast<size_t>(e.first)] = e.second;
            const ThrowLoopPlan plan = throwLoopPlan(doc.throwNext, writtenLoop.jugglers);
            if (!plan.valid || !playedLoop(writtenLoop, plan, stage.derived.paths).ok) sound.plipWanted = true;
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
    };

    markSaved();
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
                moving = moving || std::fabs(spaceMouse.move[i]) > 20.0f || std::fabs(spaceMouse.turn[i]) > 20.0f;
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
                    if (hasUnsavedChanges()) {
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
                ImGui::Separator();
                ImGui::MenuItem("Preferences...", nullptr, &ui.showPreferences);
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
                if (ImGui::MenuItem("Response Curves...")) ui.showPreferences = true;
                ImGui::Separator();
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
        refreshDerived();

        // Ladder diagram (top of left half).
        const LadderPaneRequest ladderRequest =
            drawLadderPane(ladderPane, doc, stage, stageView, gridPane.siteswapHoverThrow, playback, settings, writtenLoop,
                           shownLoop(), ImVec2(0.0f, top), ImVec2(leftWidth, bodyBottom - top), paneFlags);
        applyLadderRequest(ladderRequest, writtenLoop);

        // The siteswap grid (along the bottom, full width).
        const SiteswapGridResult gridRequest =
            drawGridPane(gridPane, doc, stage, playback, settings, writtenLoop, shownLoop(), ImVec2(0.0f, bodyBottom),
                         ImVec2(W, entryHeight), paneFlags);
        applyGridRequest(gridRequest, writtenLoop);

        // --- The scene at this moment ---
        // The jugglers juggle the pattern, or the sketch as far as it goes (props pop in and
        // vanish where throws aren't decided yet), or, while drawing, what a click would make.
        // (While the pattern has a problem, the pattern as it last worked: see below.)
        const JugglingLoop loop =
            ladderPane.ladderPreview
                ? ladderPane.ladderPreviewLoop
                : (doc.sketching ? doc.sketchLoop : (doc.patternValid ? patternLoop(doc.pattern) : JugglingLoop()));
        // Choreography fixes the distance between the jugglers once anything in it has changed
        // (a mark moved, added or deleted, a keyframe): it puts them where they stand, in meters.
        // Until then it's just the default formation, kept up with the distance.
        // A choreography saved without a length gets one now (enough whole periods to hold its
        // keyframes), so it doesn't change when the siteswap does. (It's not an unsaved change.)
        if (!ladderPane.ladderPreview && !loop.empty() && doc.choreography.active() && doc.choreography.cycle == 0) {
            const std::string before = choreographyText();
            const Choreography beforeChoreography = doc.choreography;
            doc.choreography.cycle = choreographyCycle(loop, doc.juggleParams);
            if (doc.saved.choreography == beforeChoreography) doc.saved.choreography = doc.choreography;
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
        // What the stage plays: the pattern as it last worked (all of it: throws, throwing loop and
        // walking loop together). While the one being edited has a problem the stage stays on
        // that, tinted red, and pauses as it breaks: editing, you're usually looking at a moment,
        // to see where everyone is. Play again whenever you like.
        refreshDerived();
        if (stage.derived.ok()) {
            if (!stage.haveLastGood || !stage.lastGood.derivesSameAs(stage.derivedFor)) {
                stage.lastGood = stage.derivedFor;
                stage.lastGoodDerived = stage.derived;
            }
            stage.haveLastGood = true;
        } else if (!stage.broken) {
            playback.playing = false;
        }
        stage.broken = !stage.derived.ok();
        // ...playing it out; or the ladder's preview.
        const JugglingLoop stageLoop = ladderPane.ladderPreview ? loop : stage.lastGoodDerived.played.loop;
        const bool showJugglers = !stageLoop.empty();
        const bool loopComplete = !stageLoop.empty() && openThrowCount(stageLoop) == 0;
        if (stageView.selectedJuggler >= std::max(loop.jugglers, doc.pattern.jugglers) ||
            std::max(loop.jugglers, doc.pattern.jugglers) < 2) {
            stageView.selectedJuggler = -1;
            stageView.frameSelected = false;
        }
        // What the stage is evaluated with: the choreography as it last worked.
        JuggleParams stageParams = doc.juggleParams;
        stageParams.choreography = &stage.lastGood.choreography;
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

        // Choreography mode: the paths the jugglers walk.
        // (While the throws have an error, the paths of the pattern as it last worked: the stage's.)
        const JugglingLoop& pathLoop = loop.empty() ? stageLoop : loop;
        const bool showPaths = stageView.choreographyMode && doc.choreography.active() && !pathLoop.empty();
        const int pathLength =
            showPaths ? std::max(1, choreographyCycle(pathLoop, doc.juggleParams)) : 0;    // (written out)
        const int pathCycle = choreographyFullCycle(stage.lastGood.choreography, pathLength);  // (the walking loop too)
        const ChoreographyPaths paths = showPaths ? choreographyPaths(stage.lastGood.choreography, pathLoop.jugglers,
                                                                      pathLength, choreographyScale(doc, pathLoop))
                                                  : ChoreographyPaths();

        // The 3D view (the juggler pane, less the transport bar under it): what the mouse does there.
        const float transportHeightForInput = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        const ImVec2 viewMin(leftWidth, top);
        const ImVec2 viewSize(W - leftWidth, bodyBottom - top - transportHeightForInput);
        const StageFrame frame{scene, loop, paths, showPaths, pathCycle, viewMin, viewSize};
        const StagePaneRequest stageRequest = drawStagePane(stageView, doc, playback, settings, frame, paneFlags);
        applyStageRequest(stageRequest, loop);
        const int hoveredPathPoint = stageRequest.hoveredPathPoint;

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
                const ChoreographyPaths trialPaths = choreographyPaths(trial, loop.jugglers, cycle, choreographyScale(doc, loop));
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
        // Edit > Preferences: over the ladder pane, so the stage stays in view while the 3D mouse's
        // curves are tried out.
        if (ui.showPreferences &&
            drawPreferencesWindow(&ui.showPreferences, ui.preferences, settings, spaceMouse, ImVec2(0.0f, top),
                                  ImVec2(leftWidth, bodyBottom - top)))
            saveSettings(settings);
        // Help > 3D Mouse Diagnostics: what the cap is sending, live (raw counts on each axis).
        if (ui.showSpaceMouseDiagnostics) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("3D Mouse Diagnostics", &ui.showSpaceMouseDiagnostics,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                if (!spaceMouse.present) {
                    ImGui::TextUnformatted("No 3D mouse found.");
                } else {
                    ImGui::TextUnformatted("Move the cap: what JuggleSim reads on each axis (raw counts).");
                    ImGui::Text("Slide   right %+5.0f   away %+5.0f   up %+5.0f", spaceMouse.move[0], spaceMouse.move[1],
                                spaceMouse.move[2]);
                    ImGui::Text("Turn    tilt  %+5.0f   roll %+5.0f   spin %+5.0f", spaceMouse.turn[0],
                                spaceMouse.turn[1], spaceMouse.turn[2]);
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
                        doc.saved.choreography != currentDocument().choreography ? " (and its choreography)"
                        : doc.saved.throwNext != doc.throwNext                ? " (and its throwing loop)"
                                                                              : "",
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

        // The camera (after this frame's orbit and zoom, and the 3D mouse), and what's drawn over
        // the 3D view: floor overlays, the red tint, the juggler numbers.
        // (While calibrating, the cap is pushed hard every way: the camera stays put.)
        const SpaceMouseState cameraMouse =
            ui.showPreferences && ui.preferences.calibrating ? SpaceMouseState() : spaceMouse;
        const CameraView cameraView =
            updateStageCamera(stageView, doc, stage, settings, frame, cameraMouse, ui.loiter.open);
        drawStageOverlays(stageView, doc, stage, settings, frame, cameraView, hoveredPathPoint, styleOfBall);
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
            const bool unsaved = hasUnsavedChanges();
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
