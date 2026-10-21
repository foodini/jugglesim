// app_widgets.cpp - see app_widgets.h.

#include "app_widgets.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

void drawTransportIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, TransportIcon icon, ImU32 color) {
    const float h = max.y - min.y;
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float s = h * 0.28f;  // half-size of the icon
    auto triangleRight = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx - s * 0.7f, c.y - s), ImVec2(cx + s * 0.8f, c.y),
                              ImVec2(cx - s * 0.7f, c.y + s), color);
    };
    auto triangleLeft = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx + s * 0.7f, c.y - s), ImVec2(cx - s * 0.8f, c.y),
                              ImVec2(cx + s * 0.7f, c.y + s), color);
    };
    auto bar = [&](float cx) {
        dl->AddRectFilled(ImVec2(cx - s * 0.18f, c.y - s), ImVec2(cx + s * 0.18f, c.y + s), color);
    };
    switch (icon) {
        case TransportIcon::ToStart: bar(c.x - s * 1.05f); triangleLeft(c.x - s * 0.05f); triangleLeft(c.x + s * 0.95f); break;
        case TransportIcon::StepBack: bar(c.x - s * 0.75f); triangleLeft(c.x + s * 0.35f); break;
        case TransportIcon::Play: triangleRight(c.x + s * 0.1f); break;
        case TransportIcon::Pause: bar(c.x - s * 0.4f); bar(c.x + s * 0.4f); break;
        case TransportIcon::StepForward: triangleRight(c.x - s * 0.35f); bar(c.x + s * 0.75f); break;
    }
}

}  // namespace

bool transportButton(const char* id, TransportIcon icon, float size) {
    const bool pressed = ImGui::Button(id, ImVec2(size * 1.6f, size));
    drawTransportIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      icon, ImGui::GetColorU32(ImGuiCol_Text));
    return pressed;
}

// ---- The siteswap grid's messages (the grid itself is siteswap_grid.h)

std::string describeThrow(const JugglingLoop& loop, int juggler, int beat) {
    const LoopThrow& t = loop.at(juggler, beat);
    auto handName = [&loop](int j, int b) { return loop.rightHandBeat(j, b) ? "right" : "left"; };
    char text[160];
    int n = 0;
    if (loop.jugglers > 1) n = std::snprintf(text, sizeof(text), "J%d, ", juggler + 1);
    n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "beat %d, %s hand: ", beat + 1, handName(juggler, beat));
    char* rest = text + n;
    const size_t room = sizeof(text) - static_cast<size_t>(n);
    if (t.value == kOpenThrow) {
        std::snprintf(rest, room, "? (not decided yet)");
    } else if (t.value == 0) {
        std::snprintf(rest, room, "0, an empty hand");
    } else {
        const char digit = static_cast<char>(t.value < 10 ? '0' + t.value : 'a' + t.value - 10);
        const bool pass = t.dest != juggler;
        const int caught = beat + t.value;
        if (pass)
            std::snprintf(rest, room, "%cp, a pass to J%d's %s hand, caught on beat %d", digit, t.dest + 1,
                          handName(t.dest, caught), caught + 1);
        else if (caught % 2 == beat % 2)  // (the same juggler, so a swap changes nothing here)
            std::snprintf(rest, room, "%c, back to the same hand on beat %d", digit, caught + 1);
        else
            std::snprintf(rest, room, "%c, across to the %s hand on beat %d", digit, handName(juggler, caught), caught + 1);
    }
    return text;
}

void drawShortcutsWindow(bool* open) {
    struct Entry {
        const char* keys;
        const char* what;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };
    static const std::vector<Group> kGroups = {
        {"Anywhere (Space, Ctrl+S and Ctrl+Z work in the siteswap grid too)",
         {{"Space", "Play / pause"},
          {"Left / Right", "Step back / forward 1/12 of a beat (pauses)"},
          {"Shift+Left / Shift+Right", "Step a whole beat"},
          {"B", "Back to beat 1 (and the ladder to its start)"},
          {"S", "Slow motion on or off (the rate is beside the Slow button)"},
          {"A", "Metronome on or off: a tick every beat while playing, stronger on each count's first"},
          {"Ctrl+S", "Save (over the open one of My Patterns, else asks for a name)"},
          {"Ctrl+Shift+S", "Save As (to My Patterns, under a new name)"},
          {"Ctrl+Z", "Undo"},
          {"Ctrl+Y, Ctrl+Shift+Z", "Redo"},
          {"?", "This sheet"}}},
        {"What's shown",
         {{"V", "Throw values on the ladder and over the props"},
          {"O", "Color by orbit"},
          {"L", "Dim linked jugglers on the ladder"},
          {"C", "Collapse or expand every juggler's ladder strip"},
          {"Ctrl+1 ... Ctrl+6", "Collapse or expand that juggler's ladder strip (not in choreography: groups there)"},
          {"M", "Choreography (Movement, spike Marks): show the spike marks"},
          {"K", "Keyframes on the ladder: show their spike marks (M3)"},
          {"T", "Top view"},
          {"F", "Frame the selected juggler (no one selected: everyone)"}}},
        {"Juggler pane (mouse)",
         {{"Right-drag (or Alt+drag)", "Orbit the camera"},
          {"Middle-drag, Shift+right-drag", "Pan (drags the floor along; frees the camera)"},
          {"Wheel", "Zoom (free camera: move in / out)"},
          {"3D mouse (SpaceMouse)", "Grab the stage (frees the camera): spin, tilt: turn it round its middle; "
                                    "push away / pull: move back / in; slide: move the stage"},
          {"Home", "Reset the camera (back to automatic framing)"},
          {"Click a juggler", "Select them (click empty space to deselect)"},
          {"Double-click", "Frame that juggler (empty space: frame everyone)"},
          {"Rest on a club or ring (paused)", "Its throw and spin rate"},
          {"Choreography: drag a juggler", "A keyframe for them on this beat (drop on a mark to use it)"},
          {"Choreography: click a path's beat point", "Go to that beat, with that juggler selected"},
          {"Choreography: click a mark or its name", "Select it (Shift+click: add or remove marks; Esc clears)"},
          {"Choreography: Ctrl+1 ... Ctrl+9, Ctrl+0", "Make the selected marks that group (nothing selected: empty it)"},
          {"Choreography: 1 ... 9, 0", "Select that group of marks (Shift: add it to the selection)"},
          {"Choreography: hold Ctrl", "Show every group: each mark's groups under its name, each group's marks joined up"},
          {"Choreography: drag a mark", "Move it, and any others selected with it (its arrow: turn it); Shift: off the grid"},
          {"Choreography: right-click", "Add a mark (on the floor), or a mark's menu (without dragging)"}}},
        {"Ladder (mouse)",
         {{"Wheel", "Scroll through time"},
          {"Ctrl+wheel", "Zoom"},
          {"Home, Ctrl+0", "Reset the ladder view, and the playhead back to beat 1"},
          {"Click near a throw's end", "Pick it up; click again to put it down"},
          {"Shift+click a square", "Close the edit a loop later (adds or removes props)"},
          {"Esc, right-click", "Cancel an edit (or stop drawing a sketch)"},
          {"Click / drag the beat numbers", "Go to that beat / move the playhead"},
          {"Drag a keyframe diamond", "Move the keyframe to another beat"},
          {"Ctrl+drag a keyframe diamond", "Loiter: a copy on the same spot, up to the keyframes either side"},
          {"Right-click", "Menus: a throw, a beat line, a juggler's number, a keyframe"},
          {"Drag a keyframe diamond", "Move it to another beat"},
          {"Rest on a throw", "What it is (with clubs or rings, its spin)"}}},
        {"Siteswap grid",
         {{"Type a throw", "Replaces the cell's throw; the next throw typed goes in the next cell (531: three cells)"},
          {"p, then the target", "A pass (3p, 3p2, 3p+1); with two jugglers it's done at the p"},
          {"Arrows, Tab, Home / End", "Move (Shift+arrows: select); the greyed 3s of a count are skipped"},
          {"Backspace, Delete", "Clear the selected cells (blank: not decided yet)"},
          {"Ctrl+Up / Ctrl+Down", "The throw one higher / lower"},
          {"Ctrl+Shift+Up / Down", "Where it goes (self, then each juggler)"},
          {"Ctrl+T", "Swap where two throws land (the selection's ends, or the cell and the one before)"},
          {"Ctrl+R / Ctrl+L", "Rotate the pattern right / left (a whole count at a time)"},
          {"Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V", "Select all, copy, cut, paste (siteswap text; select all to paste a whole pattern)"},
          {"Ctrl+click a throw", "Make the cursor's juggler a copy of that juggler, from that throw on"},
          {"Right-click", "A cell: insert or delete columns; a row (T1...): add, remove, link"},
          {"The \"next\" column (T rows)", "Which T row whoever's on this one goes on to after the period (2); Backspace: its own"},
          {"Esc", "Stop typing a throw (puts the cell back), then give the keyboard back"},
          {"Rest on a throw", "What it does"}}},
        {"Choreography rows (under the throws)",
         {{"Type a mark's name", "A keyframe on that mark, that beat (two characters: 0b; or one, then Tab or an arrow: 5 is 05)"},
          {"~", "Turn the long way round on the way to this keyframe"},
          {"Backspace, Delete", "Clear the keyframe"},
          {"Arrows, Tab, Home / End", "Move; the stage goes to the cursor's beat (paused)"},
          {"Ctrl+click a beat", "The cursor's juggler walks like that juggler, from that beat on"},
          {"The \"next\" column (C rows)", "Which C row whoever's on this one goes on to after the beats written out, and the turn (2, 2r+1/4, r1/2)"},
          {"Right-click", "A cell: insert or delete a beat, Walk Like..., Unlink; the next column: a builder"}}},
    };
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 46.0f, ImGui::GetFontSize() * 40.0f), ImGuiCond_FirstUseEver);
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Keyboard Shortcuts", open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("On a Mac, Ctrl is Cmd. Press ? or Esc to close.");
        for (const Group& g : kGroups) {
            ImGui::SeparatorText(g.title);
            if (ImGui::BeginTable(g.title, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 17.0f);
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
                for (const Entry& e : g.entries) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(e.keys);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", e.what);
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            *open = false;
    }
    ImGui::End();
}
