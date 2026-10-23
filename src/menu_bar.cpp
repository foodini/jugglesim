// menu_bar.cpp - see menu_bar.h.

#include "menu_bar.h"

#include <string>

MenuBarRequest drawMenuBar(const DocumentState& doc, const StageView& stageView, const LadderPane& ladderPane,
                           const Playback& playback, bool patternIsOpen, bool spaceMousePresent, LibraryState& library,
                           UiState& ui, AppSettings& settings) {
    MenuBarRequest r;
    if (!ImGui::BeginMainMenuBar()) return r;
    // (A chosen pattern is copied: loading it changes the Recent list the choice may point into.)
    auto choose = [&](const LibraryPattern& pattern, bool mine) {
        r.loadPattern = true;
        r.pattern = pattern;
        r.patternIsMine = mine;
    };
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Pattern...")) r.newPattern = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("JuggleSim Patterns")) {
            const PatternChoice choice = drawJuggleSimPatternsMenu(
                library.builtInPatterns, library.juggleSimMenuState, settings.colorVision);
            if (choice.pattern) choose(*choice.pattern, choice.mine);
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
                choose(chosen, choice.mine);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        {
            const bool open = patternIsOpen;
            const std::string saveLabel =
                open ? "Save \"" + shortMenuText(doc.openPatternName) + "\"" : std::string("Save...");
            if (ImGui::MenuItem(saveLabel.c_str(), "Ctrl+S", false, doc.patternValid || doc.sketching))
                r.save = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(open ? "Save the changes to this pattern in My Patterns."
                                       : "Save to My Patterns (it isn't one of them yet, so this asks for a name).");
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, doc.patternValid || doc.sketching))
                r.saveAs = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Save to My Patterns under a new name (or over another one).");
        }
        ImGui::MenuItem("Manage My Patterns...", nullptr, &library.manageWindow.open);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) r.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        // (Named by what they'd undo or redo: "Undo Tempo 150 to 160".)
        const bool chain = ladderPane.ladderEdit.chain.active;  // (undo cancels it)
        const std::string undoLabel =
            chain ? std::string("Undo Ladder Edit")
                  : doc.history.undo.empty() ? std::string("Undo")
                                             : "Undo " + shortMenuText(doc.history.undo.back().label);
        if (ImGui::MenuItem((undoLabel + "###undo").c_str(), "Ctrl+Z", false, !doc.history.undo.empty() || chain))
            r.undo = true;
        const std::string redoLabel =
            doc.history.redo.empty() ? std::string("Redo") : "Redo " + shortMenuText(doc.history.redo.back().label);
        if (ImGui::MenuItem((redoLabel + "###redo").c_str(), "Ctrl+Y", false, !doc.history.redo.empty())) r.redo = true;
        ImGui::Separator();
        ImGui::MenuItem("Preferences...", nullptr, &ui.showPreferences);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Playback")) {
        if (ImGui::MenuItem(playback.playing ? "Pause" : "Play", "Space"))
            r.togglePlaying = true;
        if (ImGui::MenuItem("Step Forward", "Right")) r.step = kStepBeats;
        if (ImGui::MenuItem("Step Back", "Left")) r.step = -kStepBeats;
        if (ImGui::MenuItem("Step Forward One Beat", "Shift+Right")) r.step = 1.0;
        if (ImGui::MenuItem("Step Back One Beat", "Shift+Left")) r.step = -1.0;
        if (ImGui::MenuItem("Go to Beat 1", "B")) r.goToStart = true;
        if (ImGui::MenuItem("Slow Motion", "S", playback.slow)) r.toggleSlow = true;
        if (ImGui::MenuItem("Metronome", "A", settings.metronome)) {
            settings.metronome = !settings.metronome;
            r.settingsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("A tick on every beat while playing, a stronger one on the first beat of each count.");
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Tweakables")) r.resetTweakables = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Tempo 150 BPM, dwell 1.60 beats, automatic distance");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Props")) {
        for (int i = 0; i < static_cast<int>(PropType::Count); ++i) {
            const PropType prop = static_cast<PropType>(i);
            if (ImGui::MenuItem(propTypeName(prop), nullptr, settings.prop == prop) &&
                settings.prop != prop) {
                r.setProp = true;
                r.prop = prop;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Ladder View", "Home over ladder", false,
                            !ladderViewIsDefault(ladderPane.ladderEdit)))
            r.resetLadderView = true;
        if (ImGui::MenuItem("Throw Values", "V", settings.showThrowValues)) {
            settings.showThrowValues = !settings.showThrowValues;
            r.settingsChanged = true;
        }
        if (ImGui::MenuItem("Reset Layout", nullptr, false, settings.ladderFraction != 0.5f)) {
            settings.ladderFraction = 0.5f;
            r.settingsChanged = true;
        }
        if (ImGui::MenuItem("Reset Camera", "Home over juggler")) r.resetCamera = true;
        if (ImGui::MenuItem("Frame All", nullptr, false, stageView.frameSelected || stageView.camera.free))
            r.frameAll = true;
        if (ImGui::MenuItem("Frame Selected", "F", false, stageView.selectedJuggler >= 0)) r.frameSelected = true;
        if (ImGui::MenuItem("Top View", "T", stageView.camera.topView)) r.toggleTopView = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Look straight down at the floor (for placing spike marks and jugglers).");
        ImGui::Separator();
        if (ImGui::MenuItem("Choreography", "M", stageView.choreographyMode && doc.choreography.active()))
            r.toggleChoreographyMode = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Shows the spike marks on the floor, to move, turn and put jugglers on.\n"
                              "Pause on a beat, then drag a juggler to where they should be then (onto a\n"
                              "mark, or anywhere). Click marks to select them (Shift+click: more) and drag one\n"
                              "to move them all. T: the top view.");
        if (ImGui::MenuItem("Keyframe Marks", "K", settings.showKeyframeMarks)) {
            settings.showKeyframeMarks = !settings.showKeyframeMarks;
            r.settingsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("On the ladder: \"M3\" beside each keyframe on a spike mark.");
        if (ImGui::MenuItem("Path Beat Numbers", nullptr, settings.pathBeatNumbers,
                            stageView.choreographyMode)) {
            settings.pathBeatNumbers = !settings.pathBeatNumbers;
            r.settingsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Choreography: number every beat along the jugglers' paths.\n"
                              "Off: only the point under the mouse is numbered.");
        if (ImGui::MenuItem("Clear Choreography", nullptr, false, doc.choreography.active()))
            r.clearChoreography = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Color by Orbit", "O", settings.colorByOrbit)) {
            settings.colorByOrbit = !settings.colorByOrbit;
            r.settingsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("One color per orbit (the throws a group of props travel round)\n"
                              "instead of one per prop. Sketches are always colored by path.");
        {
            const int jugglers =
                doc.sketching ? doc.sketchLoop.jugglers : (doc.patternValid ? doc.pattern.jugglers : 0);
            const bool expand = allStripsCollapsed(ladderPane.ladderEdit, jugglers);
            if (ImGui::MenuItem(expand ? "Expand All Jugglers" : "Collapse All Jugglers", "C", false, jugglers > 1))
                r.toggleCollapseAll = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Narrows every juggler's strip on the ladder (their throws still show),\n"
                                  "or widens them all again if they're all narrow.");
        }
        if (ImGui::MenuItem("Dim Linked Jugglers", "L", settings.dimLinked, doc.parsed.form.hasLinks())) {
            settings.dimLinked = !settings.dimLinked;
            r.settingsChanged = true;
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
                    r.settingsChanged = true;
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("Preview all modes...", nullptr, &ui.showColorPreview);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Sounds", nullptr, settings.sounds)) {
            settings.sounds = !settings.sounds;
            r.settingsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("A short plip when something you just did is refused, or breaks the pattern.");
        ImGui::Separator();
        ImGui::MenuItem("ImGui demo window", nullptr, &ui.showImGuiDemo);
        ImGui::EndMenu();
    }
    if (spaceMousePresent && ImGui::BeginMenu("3D Mouse")) {
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
        ImGui::MenuItem("3D Mouse Diagnostics", nullptr, &ui.showSpaceMouseDiagnostics, spaceMousePresent);
        ImGui::EndMenu();
    }
    r.height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
    return r;
}
