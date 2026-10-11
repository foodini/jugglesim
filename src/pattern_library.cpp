// pattern_library.cpp - see pattern_library.h.
#include "pattern_library.h"

#include "pattern.h"
#include "settings.h"
#include "siteswap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace {

std::string trim(const std::string& s) {
    const char* kSpace = " \t\r\n";
    const size_t first = s.find_first_not_of(kSpace);
    if (first == std::string::npos) return std::string();
    const size_t last = s.find_last_not_of(kSpace);
    return s.substr(first, last - first + 1);
}

// Splits on ';' into at most three fields (siteswap, name, settings).
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (fields.size() < 2) {
        const size_t semi = line.find(';', start);
        if (semi == std::string::npos) break;
        fields.push_back(trim(line.substr(start, semi - start)));
        start = semi + 1;
    }
    fields.push_back(trim(line.substr(start)));
    return fields;
}

bool parseNumber(const std::string& text, double* value) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;
    *value = v;
    return true;
}

std::string formatNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

void parseSettings(const std::string& text, LibraryPattern* pattern) {
    std::istringstream in(text);
    std::string item;
    while (in >> item) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = item.substr(0, eq);
        const std::string value = item.substr(eq + 1);
        PatternSettings& s = pattern->settings;
        double number = 0.0;
        if (key == "props") {
            PropType prop;
            if (propTypeFromKey(value.c_str(), &prop)) {
                s.hasProp = true;
                s.prop = prop;
            }
        } else if (key == "tempo") {
            if (parseNumber(value, &number) && number >= 40.0 && number <= 420.0) {
                s.hasTempo = true;
                s.tempo = number;
            }
        } else if (key == "dwell") {
            if (parseNumber(value, &number) && number >= 0.1 && number <= 1.9) {
                s.hasDwell = true;
                s.dwell = number;
            }
        } else if (key == "distance") {
            if (value == "auto") {
                s.hasDistance = true;
                s.distance = 0.0;
            } else if (parseNumber(value, &number) && number >= kMinPassingDistance &&
                       number <= kMaxPassingDistance) {
                s.hasDistance = true;
                s.distance = number;
            }
        } else if (key == "choreo") {
            s.hasChoreography = true;
            s.choreography = value;
        } else if (key == "camera") {
            // x,y,z,yaw,pitch (m, degrees)
            double v[5];
            size_t start = 0;
            int n = 0;
            bool ok = true, ended = false;
            while (ok && !ended && n < 5) {
                const size_t comma = value.find(',', start);
                const std::string part = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                ok = parseNumber(part, &v[n]) && std::fabs(v[n]) < 1000.0;
                ++n;
                ended = comma == std::string::npos;
                start = comma + 1;
            }
            if (ok && ended && n == 5) {
                s.hasCamera = true;
                std::copy(v, v + 5, s.camera);
            } else {
                pattern->otherSettings.emplace_back(key, value);
            }
        } else {
            pattern->otherSettings.emplace_back(key, value);
        }
    }
}

}  // namespace

void classifyPattern(LibraryPattern* pattern) {
    pattern->jugglers = pattern->props = pattern->period = 0;
    const Siteswap parsed = parseSiteswap(pattern->siteswap);
    if (parsed.sketch) {  // props aren't known yet: grouped as sketches (props 0)
        pattern->jugglers = parsed.jugglers();
        pattern->period = parsed.loop.period;
        return;
    }
    if (!parsed.valid) return;
    const Pattern p = patternFromSiteswap(parsed);
    pattern->jugglers = parsed.jugglers();
    pattern->props = parsed.ballCount;
    pattern->period = shortestPeriodBeats(p);
}

std::vector<LibraryPattern> parsePatternLibrary(const std::string& text) {
    std::vector<LibraryPattern> patterns;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::vector<std::string> fields = splitFields(trimmed);
        if (fields[0].empty()) continue;
        LibraryPattern pattern;
        pattern.siteswap = fields[0];
        if (fields.size() > 1) pattern.name = sanitizePatternName(fields[1]);
        if (fields.size() > 2) parseSettings(fields[2], &pattern);
        classifyPattern(&pattern);
        patterns.push_back(pattern);
    }
    return patterns;
}

std::string formatPatternLine(const LibraryPattern& pattern) {
    std::string line = pattern.siteswap + " ; " + sanitizePatternName(pattern.name);
    std::string settings;
    auto add = [&settings](const std::string& key, const std::string& value) {
        if (!settings.empty()) settings += ' ';
        settings += key + '=' + value;
    };
    const PatternSettings& s = pattern.settings;
    if (s.hasProp) add("props", propTypeKey(s.prop));
    if (s.hasTempo) add("tempo", formatNumber(s.tempo));
    if (s.hasDwell) add("dwell", formatNumber(s.dwell));
    if (s.hasDistance) add("distance", s.distance > 0.0 ? formatNumber(s.distance) : std::string("auto"));
    if (s.hasChoreography && !s.choreography.empty()) add("choreo", s.choreography);
    if (s.hasCamera) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f,%.1f,%.1f", s.camera[0], s.camera[1], s.camera[2],
                      s.camera[3], s.camera[4]);
        add("camera", buffer);
    }
    for (const std::pair<std::string, std::string>& other : pattern.otherSettings)
        add(other.first, other.second);
    if (!settings.empty()) line += " ; " + settings;
    return line;
}

std::string formatUserPatternFile(const std::vector<LibraryPattern>& patterns) {
    std::string text =
        "# My JuggleSim patterns. JuggleSim rewrites this file when you save, rename or delete\n"
        "# a pattern (File > Save, File > Save As, File > Manage My Patterns).\n"
        "# One pattern per line:   siteswap ; name ; settings\n";
    for (const LibraryPattern& pattern : patterns) text += formatPatternLine(pattern) + "\n";
    return text;
}

std::string sanitizePatternName(const std::string& name) {
    std::string clean;
    for (const char c : name) {
        if (c == ';') continue;
        clean += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;  // tabs etc. become spaces
    }
    return trim(clean);
}

namespace {

// Writes `text` to the user data folder (userDataPath, settings.h): to a temporary file first, then swapped in,
// so a failed write can't leave a half-written file.
bool writeUserFile(const wchar_t* fileName, const std::string& text) {
    const std::filesystem::path path = userDataPath(fileName);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temp = path;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    std::filesystem::rename(temp, path, ec);  // replaces the old file
    return !ec;
}

std::string readUserFile(const wchar_t* fileName) {
    std::ifstream in(userDataPath(fileName), std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::vector<LibraryPattern> loadUserPatterns() { return parsePatternLibrary(readUserFile(L"my_patterns.txt")); }

bool saveUserPatterns(const std::vector<LibraryPattern>& patterns) {
    return writeUserFile(L"my_patterns.txt", formatUserPatternFile(patterns));
}

void addRecentPattern(std::vector<RecentPattern>* recent, const LibraryPattern& pattern, bool mine) {
    const std::string line = formatPatternLine(pattern);
    const std::string name = pattern.displayName();
    for (size_t i = 0; i < recent->size();) {
        const RecentPattern& old = (*recent)[i];
        const bool same = old.mine == mine &&
                          (mine ? old.pattern.displayName() == name : formatPatternLine(old.pattern) == line);
        if (same)
            recent->erase(recent->begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
    RecentPattern entry;
    entry.pattern = pattern;
    entry.mine = mine;
    recent->insert(recent->begin(), entry);
    if (recent->size() > static_cast<size_t>(kMaxRecentPatterns)) recent->resize(kMaxRecentPatterns);
}

void removeRecentPattern(std::vector<RecentPattern>* recent, const std::string& name) {
    for (size_t i = 0; i < recent->size();) {
        if ((*recent)[i].mine && (*recent)[i].pattern.displayName() == name)
            recent->erase(recent->begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

void refreshRecentPattern(std::vector<RecentPattern>* recent, const std::string& oldName,
                          const LibraryPattern& current) {
    bool kept = false;
    for (size_t i = 0; i < recent->size();) {
        RecentPattern& entry = (*recent)[i];
        const bool match = entry.mine && (entry.pattern.displayName() == oldName ||
                                          entry.pattern.displayName() == current.displayName());
        if (match && kept) {
            recent->erase(recent->begin() + static_cast<std::ptrdiff_t>(i));  // one entry per name
            continue;
        }
        if (match) {
            entry.pattern = current;
            kept = true;
        }
        ++i;
    }
}

std::vector<RecentPattern> loadRecentPatterns() {
    std::vector<RecentPattern> recent;
    std::istringstream in(readUserFile(L"recent_patterns.txt"));
    std::string line;
    while (std::getline(in, line) && recent.size() < static_cast<size_t>(kMaxRecentPatterns)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const size_t space = trimmed.find(' ');
        if (space == std::string::npos) continue;
        const std::string source = trimmed.substr(0, space);
        if (source != "mine" && source != "jugglesim") continue;
        const std::vector<LibraryPattern> parsed = parsePatternLibrary(trimmed.substr(space + 1));
        if (parsed.size() != 1 || parsed[0].jugglers == 0) continue;
        RecentPattern entry;
        entry.pattern = parsed[0];
        entry.mine = source == "mine";
        bool listed = false;  // (older files could list one of My Patterns more than once)
        for (const RecentPattern& earlier : recent)
            if (entry.mine && earlier.mine && earlier.pattern.displayName() == entry.pattern.displayName()) listed = true;
        if (!listed) recent.push_back(entry);
    }
    return recent;
}

bool saveRecentPatterns(const std::vector<RecentPattern>& recent) {
    std::string text = "# Recently loaded JuggleSim patterns, most recent first.\n";
    for (const RecentPattern& entry : recent)
        text += std::string(entry.mine ? "mine " : "jugglesim ") + formatPatternLine(entry.pattern) + "\n";
    return writeUserFile(L"recent_patterns.txt", text);
}
