// pattern_library_ui.h - the pattern library's menus and windows (ImGui).
//
// Nothing here changes the library or the pattern itself: each function returns a request
// describing what the user asked for, and the caller (main.cpp) carries it out.
#pragma once

#include "color_vision.h"
#include "pattern_library.h"

#include <string>
#include <vector>

// The pattern menus: File > JuggleSim Patterns and File > My Patterns. Patterns are grouped
// by juggler count, then number of props, then period, with the number of patterns in each
// group shown, but the menus adapt to how many patterns there are:
//   - a level with only one choice is skipped;
//   - a group of kFlatListLimit patterns or fewer is listed directly, with headings
//     ("3 props, period 3") instead of further submenus.
// Each menu starts with a Find box that searches it by name or siteswap. My Patterns also lists
// the recently loaded patterns (from either menu) first.
//
// Call these between BeginMenu() and EndMenu(). Each returns the pattern chosen this frame.
constexpr int kFlatListLimit = 20;
constexpr int kMaxFindResults = 30;

struct PatternMenuState {
    char find[64] = {};  // what's typed in the menu's Find box
};
struct PatternChoice {
    const LibraryPattern* pattern = nullptr;  // nullptr if nothing was chosen
    bool mine = false;                         // from the user's own patterns
};
PatternChoice drawJuggleSimPatternsMenu(const std::vector<LibraryPattern>& builtIn,
                                        PatternMenuState& state, ColorVisionMode colorVision);
PatternChoice drawMyPatternsMenu(const std::vector<LibraryPattern>& mine,
                                 const std::vector<RecentPattern>& recent, PatternMenuState& state,
                                 ColorVisionMode colorVision);

// Text color for the user's own patterns (the built-in ones use the normal text color), so
// they stand out wherever the two are listed together (e.g. Recent).
ImU32 myPatternColor(ColorVisionMode colorVision);

// "Clubs, 120 BPM, dwell 1.20 beats" (only the settings that are set), or "" if none.
std::string describePatternSettings(const PatternSettings& settings);

// File > Save to My Patterns...: a small modal dialog asking for a name.
struct SavePatternDialog {
    bool openRequested = false;  // set to open it (fill in `name` with the default first)
    char name[128] = {};
    bool askReplace = false;  // the name is taken: asking whether to replace that pattern
};
struct SavePatternRequest {
    bool save = false;
    std::string name;  // sanitized, never empty
    bool replace = false;  // a pattern of yours with this name exists: replace it
};
// Call every frame.
SavePatternRequest drawSavePatternDialog(SavePatternDialog& dialog, const std::string& siteswap,
                                         const PatternSettings& settings,
                                         const std::vector<LibraryPattern>& mine);

// File > Manage My Patterns...: a table of the user's patterns with Load, Rename and Delete.
struct ManagePatternsWindow {
    bool open = false;
    int renaming = -1;  // index of the pattern being renamed
    char renameBuffer[128] = {};
    bool renameFocus = false;
    int confirmDelete = -1;  // index of the pattern whose deletion is being confirmed
};
struct ManagePatternsRequest {
    int load = -1;
    int remove = -1;
    int rename = -1;
    std::string newName;  // for rename: sanitized, never empty, not another pattern's name
};
// Call every frame (draws nothing while the window is closed).
ManagePatternsRequest drawManagePatternsWindow(ManagePatternsWindow& window,
                                               const std::vector<LibraryPattern>& mine,
                                               ColorVisionMode colorVision);
