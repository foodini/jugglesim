// pattern_library_ui.cpp - see pattern_library_ui.h.
#include "pattern_library_ui.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <map>

namespace {

// ImGui treats "##" in a label as the start of a hidden ID, so a name containing it would be
// cut short. (IDs come from PushID instead.)
std::string menuLabel(const std::string& text) {
    std::string label = text;
    size_t at = 0;
    while ((at = label.find("##", at)) != std::string::npos) label.replace(at, 2, "# #");
    return label;
}

bool lessIgnoringCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

// Modal dialogs open in the middle of the screen.
void centerNextWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

bool nameTaken(const std::vector<LibraryPattern>& mine, const std::string& name, int except) {
    for (int i = 0; i < static_cast<int>(mine.size()); ++i)
        if (i != except && mine[static_cast<size_t>(i)].displayName() == name) return true;
    return false;
}

// One pattern as a menu item: its name, with the siteswap beside it (in the shortcut column)
// when the name is something else. Hovering shows the settings it carries.
bool patternMenuItem(const LibraryPattern& p, bool mine, ImU32 myColor) {
    const bool showSiteswap = !p.name.empty() && p.name != p.siteswap;
    if (mine) ImGui::PushStyleColor(ImGuiCol_Text, myColor);
    const bool chosen = ImGui::MenuItem(menuLabel(p.displayName()).c_str(),
                                        showSiteswap ? p.siteswap.c_str() : nullptr);
    if (mine) ImGui::PopStyleColor();
    const std::string settings = describePatternSettings(p.settings);
    if (!settings.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Also sets: %s", settings.c_str());
    return chosen;
}

// ---- The adaptive pattern tree.

enum TreeLevel { kJugglerLevel = 0, kPropsLevel = 1, kPeriodLevel = 2, kPatternLevel = 3 };

int keyAt(const LibraryPattern& p, int level) {
    return level == kJugglerLevel ? p.jugglers : (level == kPropsLevel ? p.props : p.period);
}

struct MenuContext {
    bool mine = false;  // listing the user's own patterns (drawn in their color)
    ImU32 myColor = 0;
    PatternChoice* choice = nullptr;
};

void drawPatternItems(const std::vector<const LibraryPattern*>& patterns, MenuContext& ctx) {
    for (const LibraryPattern* p : patterns) {
        ImGui::PushID(p);
        if (patternMenuItem(*p, ctx.mine, ctx.myColor)) {
            ctx.choice->pattern = p;
            ctx.choice->mine = ctx.mine;
        }
        ImGui::PopID();
    }
}

// "1 juggler, 3 props, period 3", for the levels from `fromLevel` down (the juggler part only
// when the list mixes juggler counts).
std::string groupHeading(const LibraryPattern& p, int fromLevel, bool withJugglers) {
    std::string heading;
    char part[48];
    if (fromLevel <= kJugglerLevel && withJugglers) {
        std::snprintf(part, sizeof(part), "%d juggler%s", p.jugglers, p.jugglers == 1 ? "" : "s");
        heading += part;
    }
    if (fromLevel <= kPropsLevel) {
        std::snprintf(part, sizeof(part), "%s%d prop%s", heading.empty() ? "" : ", ", p.props, p.props == 1 ? "" : "s");
        heading += part;
    }
    if (fromLevel <= kPeriodLevel) {
        std::snprintf(part, sizeof(part), "%speriod %d", heading.empty() ? "" : ", ", p.period);
        heading += part;
    }
    return heading;
}

// Lists patterns (already sorted by group) directly, with a heading for each group when there's
// more than one.
void drawFlatList(const std::vector<const LibraryPattern*>& patterns, int level, MenuContext& ctx) {
    if (patterns.empty()) return;
    auto sameGroup = [level](const LibraryPattern* a, const LibraryPattern* b) {
        for (int l = level; l < kPatternLevel; ++l)
            if (keyAt(*a, l) != keyAt(*b, l)) return false;
        return true;
    };
    bool oneGroup = true, mixedJugglers = false;
    for (const LibraryPattern* p : patterns) {
        if (!sameGroup(p, patterns.front())) oneGroup = false;
        if (p->jugglers != patterns.front()->jugglers) mixedJugglers = true;
    }
    std::vector<const LibraryPattern*> group;
    for (size_t i = 0; i < patterns.size(); ++i) {
        group.push_back(patterns[i]);
        const bool groupEnds = i + 1 == patterns.size() || !sameGroup(patterns[i], patterns[i + 1]);
        if (!groupEnds) continue;
        if (!oneGroup) ImGui::SeparatorText(groupHeading(*group.front(), level, mixedJugglers).c_str());
        drawPatternItems(group, ctx);
        group.clear();
    }
}

std::string levelLabel(int level, int key, int count) {
    char label[64];
    if (level == kJugglerLevel)
        std::snprintf(label, sizeof(label), "%d Juggler%s (%d)", key, key == 1 ? "" : "s", count);
    else if (level == kPropsLevel)
        std::snprintf(label, sizeof(label), "%d Prop%s (%d)", key, key == 1 ? "" : "s", count);
    else
        std::snprintf(label, sizeof(label), "Period %d (%d)", key, count);
    return label;
}

// One level of the tree for `patterns` (sorted by juggler count, props, period). With
// `jugglerPlaceholders`, the juggler level is always shown, with 2-4 jugglers listed (greyed
// out) even when there are no such patterns yet.
void drawTreeLevel(const std::vector<const LibraryPattern*>& patterns, int level, MenuContext& ctx,
                   bool jugglerPlaceholders) {
    if (level == kPatternLevel) {
        drawPatternItems(patterns, ctx);
        return;
    }
    if (!jugglerPlaceholders && static_cast<int>(patterns.size()) <= kFlatListLimit) {
        drawFlatList(patterns, level, ctx);
        return;
    }
    std::map<int, std::vector<const LibraryPattern*>> groups;
    for (const LibraryPattern* p : patterns) groups[keyAt(*p, level)].push_back(p);
    if (!jugglerPlaceholders && groups.size() == 1) {
        drawTreeLevel(patterns, level + 1, ctx, false);
        return;
    }
    for (const std::pair<const int, std::vector<const LibraryPattern*>>& group : groups) {
        if (ImGui::BeginMenu(levelLabel(level, group.first, static_cast<int>(group.second.size())).c_str())) {
            drawTreeLevel(group.second, level + 1, ctx, false);
            ImGui::EndMenu();
        }
    }
    if (jugglerPlaceholders) {
        for (int jugglers = 2; jugglers <= 4; ++jugglers) {
            if (groups.count(jugglers)) continue;
            char label[32];
            std::snprintf(label, sizeof(label), "%d Jugglers", jugglers);
            if (ImGui::BeginMenu(label, false)) ImGui::EndMenu();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Coming with passing support");
        }
    }
}

// The patterns this version can read, sorted into tree order: by juggler count, props and
// period; within a group, the user's own by name and the built-in ones in library order.
std::vector<const LibraryPattern*> treeOrder(const std::vector<LibraryPattern>& patterns, bool byName) {
    std::vector<const LibraryPattern*> sorted;
    for (const LibraryPattern& p : patterns)
        if (p.jugglers > 0) sorted.push_back(&p);
    std::stable_sort(sorted.begin(), sorted.end(), [byName](const LibraryPattern* a, const LibraryPattern* b) {
        for (int l = kJugglerLevel; l < kPatternLevel; ++l)
            if (keyAt(*a, l) != keyAt(*b, l)) return keyAt(*a, l) < keyAt(*b, l);
        return byName && lessIgnoringCase(a->displayName(), b->displayName());
    });
    return sorted;
}

std::string lowercase(const std::string& text) {
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

// The Find box. While something is typed, lists the patterns whose name or siteswap contains
// it (instead of the tree) and returns true. `patterns` must be in tree order.
bool drawFind(PatternMenuState& state, const std::vector<const LibraryPattern*>& patterns, MenuContext& ctx) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##find", "Find (name or siteswap)", state.find, sizeof(state.find));
    const std::string query = lowercase(sanitizePatternName(state.find));
    if (query.empty()) return false;
    std::vector<const LibraryPattern*> matches;
    for (const LibraryPattern* p : patterns)
        if (lowercase(p->name).find(query) != std::string::npos ||
            lowercase(p->siteswap).find(query) != std::string::npos)
            matches.push_back(p);
    ImGui::Separator();
    if (matches.empty()) {
        ImGui::TextDisabled("No matches");
        return true;
    }
    const size_t shown = std::min(matches.size(), static_cast<size_t>(kMaxFindResults));
    // Grouped under the same headings as the tree ("3 props, period 2"), so patterns with the
    // same name (the 3-, 4- and 5-ball Showers) can be told apart.
    drawFlatList(std::vector<const LibraryPattern*>(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(shown)),
                 kJugglerLevel, ctx);
    if (matches.size() > shown)
        ImGui::TextDisabled("%d more: keep typing to narrow it down", static_cast<int>(matches.size() - shown));
    return true;
}

}  // namespace

PatternChoice drawJuggleSimPatternsMenu(const std::vector<LibraryPattern>& builtIn,
                                        PatternMenuState& state, ColorVisionMode colorVision) {
    PatternChoice choice;
    MenuContext ctx;
    ctx.mine = false;
    ctx.myColor = myPatternColor(colorVision);
    ctx.choice = &choice;
    const std::vector<const LibraryPattern*> sorted = treeOrder(builtIn, false);
    if (!drawFind(state, sorted, ctx)) {
        ImGui::Separator();
        drawTreeLevel(sorted, kJugglerLevel, ctx, true);
    }
    if (choice.pattern) state.find[0] = '\0';
    return choice;
}

PatternChoice drawMyPatternsMenu(const std::vector<LibraryPattern>& mine,
                                 const std::vector<RecentPattern>& recent, PatternMenuState& state,
                                 ColorVisionMode colorVision) {
    PatternChoice choice;
    MenuContext ctx;
    ctx.mine = true;
    ctx.myColor = myPatternColor(colorVision);
    ctx.choice = &choice;
    const std::vector<const LibraryPattern*> sorted = treeOrder(mine, true);
    if (!drawFind(state, sorted, ctx)) {
        if (!recent.empty()) {
            ImGui::SeparatorText("Recent");
            for (const RecentPattern& entry : recent) {
                ImGui::PushID(&entry);
                if (patternMenuItem(entry.pattern, entry.mine, ctx.myColor)) {
                    choice.pattern = &entry.pattern;
                    choice.mine = entry.mine;
                }
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("Saved");
        if (sorted.empty())
            ImGui::TextDisabled("None yet: File > Save to My Patterns adds the current pattern");
        else
            drawTreeLevel(sorted, kJugglerLevel, ctx, false);
    }
    if (choice.pattern) state.find[0] = '\0';
    return choice;
}

ImU32 myPatternColor(ColorVisionMode colorVision) {
    switch (colorVision) {
        case ColorVisionMode::Tritan: return IM_COL32(255, 150, 190, 255);  // pink: blue is weak
        case ColorVisionMode::Monochrome: return IM_COL32(255, 255, 160, 255);
        default: return IM_COL32(110, 190, 255, 255);  // sky blue
    }
}

std::string describePatternSettings(const PatternSettings& s) {
    std::string text;
    char buffer[64];
    if (s.hasProp) text += propTypeName(s.prop);
    if (s.hasTempo) {
        std::snprintf(buffer, sizeof(buffer), "%s%.0f BPM", text.empty() ? "" : ", ", s.tempo);
        text += buffer;
    }
    if (s.hasDwell) {
        std::snprintf(buffer, sizeof(buffer), "%sdwell %.2f beats", text.empty() ? "" : ", ", s.dwell);
        text += buffer;
    }
    return text;
}

SavePatternRequest drawSavePatternDialog(SavePatternDialog& dialog, const std::string& siteswap,
                                         const PatternSettings& settings,
                                         const std::vector<LibraryPattern>& mine) {
    SavePatternRequest request;
    const char* kTitle = "Save to My Patterns";
    if (dialog.openRequested) {
        ImGui::OpenPopup(kTitle);
        dialog.openRequested = false;
        dialog.askReplace = false;
    }
    centerNextWindow();
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return request;

    ImGui::Text("Siteswap:  %s", siteswap.c_str());
    ImGui::Text("Saved with:  %s", describePatternSettings(settings).c_str());
    ImGui::Spacing();
    const std::string name = sanitizePatternName(dialog.name);
    if (!dialog.askReplace) {
        ImGui::TextUnformatted("Name");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        const bool enter = ImGui::InputText("##name", dialog.name, sizeof(dialog.name),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::BeginDisabled(name.empty());
        const bool save = ImGui::Button("Save", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f)) || (enter && !name.empty());
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f)) ||
                            ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (save) {
            if (nameTaken(mine, name, -1)) {
                dialog.askReplace = true;
            } else {
                request.save = true;
                request.name = name;
                ImGui::CloseCurrentPopup();
            }
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::Text("You already have a pattern called \"%s\".", name.c_str());
        ImGui::TextUnformatted("Replace it with this one?");
        ImGui::Spacing();
        if (ImGui::Button("Replace", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
            request.save = true;
            request.replace = true;
            request.name = name;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Back", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            dialog.askReplace = false;
    }
    ImGui::EndPopup();
    return request;
}

ManagePatternsRequest drawManagePatternsWindow(ManagePatternsWindow& window,
                                               const std::vector<LibraryPattern>& mine,
                                               ColorVisionMode colorVision) {
    ManagePatternsRequest request;
    if (!window.open) return request;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 44.0f, em * 22.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("My Patterns", &window.open)) {
        ImGui::End();
        return request;
    }
    if (window.renaming >= static_cast<int>(mine.size())) window.renaming = -1;

    if (mine.empty()) {
        ImGui::TextWrapped("You haven't saved any patterns yet. File > Save to My Patterns adds "
                           "the current pattern, with its props, tempo and dwell.");
    } else {
        ImGui::TextDisabled("%d pattern%s. Click a column heading to sort.", static_cast<int>(mine.size()),
                            mine.size() == 1 ? "" : "s");
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("patterns", 6, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Siteswap", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed, em * 4.0f);
            ImGui::TableSetupColumn("Tempo", ImGuiTableColumnFlags_WidthFixed, em * 4.0f);
            ImGui::TableSetupColumn("Dwell", ImGuiTableColumnFlags_WidthFixed, em * 3.5f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, em * 13.0f);
            ImGui::TableHeadersRow();

            // Row order from the current sort.
            std::vector<int> order(mine.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
            int sortColumn = 0;
            bool descending = false;
            if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsCount > 0) {
                    sortColumn = specs->Specs[0].ColumnIndex;
                    descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                }
            }
            std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
                const LibraryPattern& a = mine[static_cast<size_t>(ia)];
                const LibraryPattern& b = mine[static_cast<size_t>(ib)];
                bool less = false, greater = false;
                switch (sortColumn) {
                    case 1: less = a.siteswap < b.siteswap; greater = b.siteswap < a.siteswap; break;
                    case 2: less = a.settings.prop < b.settings.prop; greater = b.settings.prop < a.settings.prop; break;
                    case 3: less = a.settings.tempo < b.settings.tempo; greater = b.settings.tempo < a.settings.tempo; break;
                    case 4: less = a.settings.dwell < b.settings.dwell; greater = b.settings.dwell < a.settings.dwell; break;
                    default:
                        less = lessIgnoringCase(a.displayName(), b.displayName());
                        greater = lessIgnoringCase(b.displayName(), a.displayName());
                        break;
                }
                return descending ? greater : less;
            });

            const ImU32 myColor = myPatternColor(colorVision);
            for (const int i : order) {
                const LibraryPattern& p = mine[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (window.renaming == i) {
                    if (window.renameFocus) {
                        ImGui::SetKeyboardFocusHere();
                        window.renameFocus = false;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const bool enter = ImGui::InputText("##rename", window.renameBuffer, sizeof(window.renameBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
                    const std::string newName = sanitizePatternName(window.renameBuffer);
                    const bool taken = nameTaken(mine, newName, i);
                    if (taken) ImGui::SetItemTooltip("You already have a pattern with that name.");
                    if (enter && !newName.empty() && !taken) {
                        request.rename = i;
                        request.newName = newName;
                        window.renaming = -1;
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        window.renaming = -1;
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, myColor);
                    ImGui::TextUnformatted(p.displayName().c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::TableSetColumnIndex(1);
                if (p.jugglers > 0) {
                    ImGui::TextUnformatted(p.siteswap.c_str());
                } else {
                    ImGui::TextDisabled("%s", p.siteswap.c_str());
                    ImGui::SetItemTooltip("This pattern uses notation this version of JuggleSim can't read yet.");
                }
                ImGui::TableSetColumnIndex(2);
                if (p.settings.hasProp) ImGui::TextUnformatted(propTypeName(p.settings.prop));
                else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(3);
                if (p.settings.hasTempo) ImGui::Text("%.0f", p.settings.tempo);
                else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(4);
                if (p.settings.hasDwell) ImGui::Text("%.2f", p.settings.dwell);
                else ImGui::TextDisabled("-");

                ImGui::TableSetColumnIndex(5);
                ImGui::BeginDisabled(p.jugglers == 0);
                if (ImGui::SmallButton("Load")) request.load = i;
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (window.renaming == i) {
                    const std::string newName = sanitizePatternName(window.renameBuffer);
                    ImGui::BeginDisabled(newName.empty() || nameTaken(mine, newName, i));
                    if (ImGui::SmallButton("OK")) {
                        request.rename = i;
                        request.newName = newName;
                        window.renaming = -1;
                    }
                    ImGui::EndDisabled();
                } else if (ImGui::SmallButton("Rename")) {
                    window.renaming = i;
                    window.renameFocus = true;
                    std::snprintf(window.renameBuffer, sizeof(window.renameBuffer), "%s", p.displayName().c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    window.confirmDelete = i;
                    ImGui::OpenPopup("Delete pattern");
                }
                if (window.confirmDelete == i) centerNextWindow();
                if (window.confirmDelete == i &&
                    ImGui::BeginPopupModal("Delete pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete \"%s\" (%s)?", p.displayName().c_str(), p.siteswap.c_str());
                    ImGui::TextUnformatted("This can't be undone.");
                    ImGui::Spacing();
                    if (ImGui::Button("Delete", ImVec2(em * 6.0f, 0.0f))) {
                        request.remove = i;
                        window.confirmDelete = -1;
                        if (window.renaming == i) window.renaming = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        window.confirmDelete = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    return request;
}
