// pattern_library.h - the pattern library: JuggleSim's built-in patterns plus the user's own.
//
// Both are plain text in the same format, one pattern per line (see data/patterns.txt):
//     siteswap ; name ; settings
// The built-in library is compiled into the executable. The user's patterns live in
// my_patterns.txt in the user data folder (see userDataPath in settings.h) and are rewritten
// whenever they change.
//
// Patterns are grouped in the menu by juggler count, number of props and period, all worked
// out from the siteswap.
#pragma once

#include "juggle_sim.h"

#include <string>
#include <utility>
#include <vector>

// Settings a pattern can carry along with its siteswap. Each is optional: loading a pattern
// changes only the settings it has.
struct PatternSettings {
    bool hasProp = false;
    PropType prop = PropType::Ball;
    bool hasTempo = false;
    double tempo = 150.0;  // beats per minute
    bool hasDwell = false;
    double dwell = 1.4;  // beats
    // Distance between passing jugglers (m); 0 = automatic. Only saved for passing patterns.
    bool hasDistance = false;
    double distance = 0.0;

    bool operator==(const PatternSettings& o) const {
        return hasProp == o.hasProp && prop == o.prop && hasTempo == o.hasTempo &&
               tempo == o.tempo && hasDwell == o.hasDwell && dwell == o.dwell &&
               hasDistance == o.hasDistance && distance == o.distance;
    }
    bool operator!=(const PatternSettings& o) const { return !(*this == o); }
};

struct LibraryPattern {
    std::string siteswap;
    std::string name;  // may be empty: the siteswap is shown instead
    PatternSettings settings;
    // Settings this version doesn't know (written by a newer JuggleSim), kept so that saving
    // the user's file never loses them.
    std::vector<std::pair<std::string, std::string>> otherSettings;

    // Worked out from the siteswap. jugglers is 0 if this version can't read the siteswap
    // (e.g. notation from a newer version): such patterns are kept but not offered. A sketch
    // (some throws "?") has props 0.
    int jugglers = 0;
    int props = 0;
    int period = 0;

    const std::string& displayName() const { return name.empty() ? siteswap : name; }
};

// Parses library text. Blank lines and # comments are skipped, as are lines with no siteswap.
std::vector<LibraryPattern> parsePatternLibrary(const std::string& text);

// One pattern as a library line (no newline), and a whole user file (with a header comment).
std::string formatPatternLine(const LibraryPattern& pattern);
std::string formatUserPatternFile(const std::vector<LibraryPattern>& patterns);

// Fills in jugglers/props/period from the siteswap.
void classifyPattern(LibraryPattern* pattern);

// A name made safe for the file: no ';' (the field separator) or control characters, trimmed.
std::string sanitizePatternName(const std::string& name);

// A recently loaded pattern: a copy of it (so it still works if the original is renamed or
// deleted), and whether it came from the user's own patterns.
struct RecentPattern {
    LibraryPattern pattern;
    bool mine = false;
};
constexpr int kMaxRecentPatterns = 8;

// Puts `pattern` at the front of `recent` (moving it there if it's already in the list) and
// trims the list to kMaxRecentPatterns.
void addRecentPattern(std::vector<RecentPattern>* recent, const LibraryPattern& pattern, bool mine);

// The recent list (recent_patterns.txt in the user data folder, most recent first). Each line is
// "mine" or "jugglesim", a space, then a library line.
std::vector<RecentPattern> loadRecentPatterns();
bool saveRecentPatterns(const std::vector<RecentPattern>& recent);

// The user's patterns (my_patterns.txt in the user data folder). A missing file is an empty list.
std::vector<LibraryPattern> loadUserPatterns();
// Writes them (to a temporary file first, then swaps it in, so a failed write can't leave a
// half-written file). Returns false if the file couldn't be written.
bool saveUserPatterns(const std::vector<LibraryPattern>& patterns);
