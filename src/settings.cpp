// settings.cpp - see settings.h.
#include "settings.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

std::filesystem::path settingsPath() { return userDataPath(L"settings.ini"); }

std::string trim(const std::string& s) {
    const char* kSpace = " \t\r\n";
    const size_t first = s.find_first_not_of(kSpace);
    if (first == std::string::npos) return std::string();
    const size_t last = s.find_last_not_of(kSpace);
    return s.substr(first, last - first + 1);
}

}  // namespace

std::filesystem::path userDataPath(const wchar_t* fileName) {
#if defined(_WIN32)
    const wchar_t* appData = _wgetenv(L"APPDATA");
    if (!appData || !*appData) return std::filesystem::path(fileName);
    return std::filesystem::path(appData) / L"JuggleSim" / fileName;
#else
    // macOS: ~/Library/Application Support/JuggleSim. (Elsewhere: ~/.config/JuggleSim, or
    // $XDG_CONFIG_HOME/JuggleSim.)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return std::filesystem::path(fileName);
#if defined(__APPLE__)
    return std::filesystem::path(home) / "Library" / "Application Support" / "JuggleSim" / fileName;
#else
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const std::filesystem::path base = (config && *config) ? std::filesystem::path(config)
                                                           : std::filesystem::path(home) / ".config";
    return base / "JuggleSim" / fileName;
#endif
#endif
}

AppSettings loadSettings() {
    AppSettings settings;
    std::ifstream in(settingsPath());
    if (!in) return settings;

    std::string line;
    while (std::getline(in, line)) {
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';') continue;
        const size_t eq = text.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(text.substr(0, eq));
        const std::string value = trim(text.substr(eq + 1));

        if (key == "color_vision") {
            ColorVisionMode mode;
            if (colorVisionModeFromKey(value.c_str(), &mode)) settings.colorVision = mode;
        } else if (key == "tempo_panel") {
            settings.tempoPanelCollapsed = value == "collapsed";
        } else if (key == "colors") {
            settings.colorByOrbit = value == "orbit";
        } else if (key == "slow_motion") {
            char* end = nullptr;
            const double rate = std::strtod(value.c_str(), &end);
            if (end != value.c_str()) settings.slowRate = static_cast<float>(std::clamp(rate, 0.05, 0.5));
        } else if (key == "linked_jugglers") {
            settings.dimLinked = value == "dim";
        } else if (key == "throw_values") {
            settings.showThrowValues = value == "on";
        } else if (key == "ladder_width") {
            char* end = nullptr;
            const double fraction = std::strtod(value.c_str(), &end);
            if (end != value.c_str() && fraction >= 0.1 && fraction <= 0.9)
                settings.ladderFraction = static_cast<float>(fraction);
        } else if (key == "props") {
            PropType prop;
            if (propTypeFromKey(value.c_str(), &prop)) settings.prop = prop;
        }
    }
    return settings;
}

bool saveSettings(const AppSettings& settings) {
    const std::filesystem::path path = settingsPath();
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# JuggleSim user settings\n";
    out << "color_vision = " << colorVisionModeKey(settings.colorVision) << "\n";
    out << "props = " << propTypeKey(settings.prop) << "\n";
    out << "tempo_panel = " << (settings.tempoPanelCollapsed ? "collapsed" : "open") << "\n";
    out << "colors = " << (settings.colorByOrbit ? "orbit" : "prop") << "\n";
    out << "throw_values = " << (settings.showThrowValues ? "on" : "off") << "\n";
    out << "linked_jugglers = " << (settings.dimLinked ? "dim" : "normal") << "\n";
    out << "slow_motion = " << settings.slowRate << "\n";
    out << "ladder_width = " << settings.ladderFraction << "\n";
    return static_cast<bool>(out);
}
