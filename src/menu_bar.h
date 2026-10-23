// menu_bar.h - the main menu bar. It draws the menus and says what was chosen (MenuBarRequest);
// the caller does it (see applyMenuRequest in app.cpp), so the pattern, playback and the views
// only change in one place. Settings and which windows are open it changes itself (they aren't
// the pattern): it says when settings changed, so the caller can save them.
#pragma once

#include "app_state.h"

#include "imgui.h"

struct MenuBarRequest {
    float height = 0.0f;           // the bar's height (the panes go under it)
    bool settingsChanged = false;  // a setting was changed (save them)
    // File
    bool newPattern = false;  // New Pattern... (offering to save first)
    bool loadPattern = false;  // a pattern chosen from JuggleSim Patterns or My Patterns:
    LibraryPattern pattern;    // ...it (a copy)
    bool patternIsMine = false;  // ...one of My Patterns
    bool save = false;
    bool saveAs = false;
    bool quit = false;  // Exit (offering to save first)
    // Edit
    bool undo = false;
    bool redo = false;
    // Playback
    bool togglePlaying = false;
    double step = 0.0;  // step this many beats (pausing), 0: none
    bool goToStart = false;
    bool toggleSlow = false;
    bool resetTweakables = false;
    // Props
    bool setProp = false;
    PropType prop = PropType::Ball;
    // View
    bool resetLadderView = false;
    bool toggleCollapseAll = false;  // the ladder's strips: collapse all, or expand all
    bool resetCamera = false;
    bool frameAll = false;
    bool frameSelected = false;
    bool toggleTopView = false;
    bool toggleChoreographyMode = false;
    bool clearChoreography = false;
};

// Draws the bar. `patternIsOpen`: what's showing is one of My Patterns (File > Save saves over
// it). `library`: its menus' state, and Manage My Patterns' window; `ui`: the windows the menus
// open.
MenuBarRequest drawMenuBar(const DocumentState& doc, const StageView& stageView, const LadderPane& ladderPane,
                           const Playback& playback, bool patternIsOpen, bool spaceMousePresent, LibraryState& library,
                           UiState& ui, AppSettings& settings);
