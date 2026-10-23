// choreography_dialogs.cpp - see choreography_dialogs.h.

#include "choreography_dialogs.h"

#include "color_vision.h"
#include "panels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

bool drawShortenDialog(ShortenDialog& d, const Choreography& choreography) {
    bool shorten = false;
    if (d.requested) {
        d.requested = false;
        ImGui::OpenPopup("Shorten the choreography?");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Shorten the choreography?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float em = ImGui::GetFontSize();
        std::string gone;
        int count = 0;
        for (size_t j = 0; j < choreography.keys.size(); ++j)
            for (const Keyframe& k : choreography.keys[j])
                if (k.beat >= d.length) {
                    gone += (count++ == 0 ? "" : ", ") + std::string("J") + std::to_string(j + 1) + " on beat " +
                            std::to_string(k.beat + 1);
                }
        ImGui::PushTextWrapPos(em * 30.0f);
        ImGui::Text("A %d-beat choreography has no room for %s %s. Delete %s?", d.length,
                    count == 1 ? "this keyframe:" : "these keyframes:", gone.c_str(), count == 1 ? "it" : "them");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Shorten", ImVec2(em * 7.0f, 0.0f))) {
            shorten = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return shorten;
}

WalkLikeRequest drawWalkLikeWindow(WalkLikeWindow& w, const Choreography& choreography, const JugglingLoop& loop,
                                   const JuggleParams& params) {
    WalkLikeRequest r;
    r.juggler = w.juggler;
    w.previewing = false;
    if (w.open) {
        const int j = w.juggler;
        const int cycle = loop.empty() ? 0 : choreographyCycle(loop, params);
        if (loop.empty() || j < 0 || j >= loop.jugglers || cycle <= 0) {
            w.open = false;
        } else {
            const float em = ImGui::GetFontSize();
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            char title[48];
            std::snprintf(title, sizeof(title), "J%d Walks Like...###walk_like", j + 1);
            if (ImGui::Begin(title, &w.open,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                WalkLink link = choreography.link(j);
                bool changed = false;
                // Hovering a choice: show where it would put them.
                auto previewIf = [&](const WalkLink& would) {
                    if (!ImGui::IsItemHovered() || would.leader < 0) return;
                    w.previewing = true;
                    w.preview = would;
                };
                // Leader: anyone whose walking doesn't already depend on this juggler.
                ImGui::TextUnformatted("Leader:");
                for (int k = 0; k < loop.jugglers; ++k) {
                    if (k == j) continue;
                    ImGui::SameLine();
                    const bool loops = choreography.dependsOn(k, j);
                    ImGui::BeginDisabled(loops);
                    char label[8];
                    std::snprintf(label, sizeof(label), "J%d", k + 1);
                    if (ImGui::RadioButton(label, link.leader == k) && link.leader != k) {
                        if (!link.active() && choreography.hasKeys(j)) {
                            w.confirmLeader = k;  // ask first: their own keyframes go
                            w.confirmRequested = true;
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
                const int full = choreographyFullCycle(choreography, cycle);
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
                    const WalkLoop w = walkLoop(choreography, loop.jugglers);
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
                    w.preview = link;
                    w.preview.turn = hoveredTurn;
                    w.previewing = true;
                }
                ImGui::TextDisabled("        about the middle of the floor, seen from above");
                ImGui::EndDisabled();
                if (changed && link.active()) {
                    r.setLink = true;
                    r.link = link;
                }
                ImGui::Spacing();
                ImGui::BeginDisabled(!link.active());
                if (ImGui::Button("Unlink", ImVec2(em * 6.0f, 0.0f))) r.unlink = true;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Write J%d's walking out as their own keyframes (nothing moves).", j + 1);
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(em * 6.0f, 0.0f))) w.open = false;

                // Linking a juggler who has keyframes of their own: ask first.
                if (w.confirmRequested) {
                    ImGui::OpenPopup("Link this juggler?");
                    w.confirmRequested = false;
                }
                if (ImGui::BeginPopupModal("Link this juggler?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    const size_t count = j < static_cast<int>(choreography.keys.size())
                                             ? choreography.keys[static_cast<size_t>(j)].size()
                                             : 0;
                    ImGui::Text("J%d's %d keyframe%s will be deleted: J%d will walk like J%d instead.", j + 1,
                                static_cast<int>(count), count == 1 ? "" : "s", j + 1,
                                w.confirmLeader + 1);
                    ImGui::Spacing();
                    if (ImGui::Button("Link", ImVec2(em * 6.0f, 0.0f))) {
                        r.setLink = true;
                        r.link = WalkLink();
                        r.link.leader = w.confirmLeader;
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
    return r;
}

bool drawLoopBuilder(LoopBuilder& b, const JugglingLoop& loop, const JuggleParams& params) {
    bool set = false;
    if (b.requested) {
        b.requested = false;
        ImGui::OpenPopup("Walking Loop");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    if (ImGui::BeginPopupModal("Walking Loop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int j = b.juggler;
        const int n = loop.empty() ? 0 : loop.jugglers;
        const int written = loop.empty() ? 0 : choreographyCycle(loop, params);
        if (j < 0 || j >= n) ImGui::CloseCurrentPopup();
        const float em = ImGui::GetFontSize();
        ImGui::Text("After beat %d, whoever's on C%d goes on to:", written, j + 1);
        for (int r = 0; r < n; ++r) {
            if (r > 0) ImGui::SameLine();
            char label[24];
            std::snprintf(label, sizeof(label), r == j ? "C%d (own)" : "C%d", r + 1);
            if (ImGui::RadioButton(label, b.k.roleFor(j) == r))
                b.k.role = r == j ? -1 : r;
        }
        ImGui::TextUnformatted("...from its beat 1, turned:");
        turnEditor("##loopTurn", &b.k.turn);
        ImGui::TextDisabled("about the middle of the floor, seen from above");
        const std::string text = continuationText(b.k, j);
        ImGui::TextDisabled("In the cell: %s", text.empty() ? (std::to_string(j + 1) + " (its own path, unturned)").c_str()
                                                           : text.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Set", ImVec2(em * 6.0f, 0.0f))) {
            set = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return set;
}

RenameMarkRequest drawRenameMarkDialog(RenameMark& d, const Choreography& choreography) {
    RenameMarkRequest r;
    if (d.requested) {
        d.requested = false;
        if (d.mark >= 0 && d.mark < static_cast<int>(choreography.marks.size())) {
            std::snprintf(d.text, sizeof(d.text), "%s",
                          choreography.marks[static_cast<size_t>(d.mark)].name.c_str());
            d.error.clear();
            ImGui::OpenPopup("Rename Spike Mark");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
    }
    if (ImGui::BeginPopupModal("Rename Spike Mark", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool exists =
            d.mark >= 0 && d.mark < static_cast<int>(choreography.marks.size());
        if (!exists) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted("Two characters, each 0-9 or a-z:");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
        const bool entered = ImGui::InputText("##markName", d.text, sizeof(d.text),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
        // Only 0-9 and a-z (capitals made small).
        {
            std::string kept;
            for (const char* ch = d.text; *ch; ++ch) {
                const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(*ch)));
                if ((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'z')) kept += lower;
            }
            std::snprintf(d.text, sizeof(d.text), "%s", kept.c_str());
        }
        if (!d.error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", d.error.c_str());
        const float em = ImGui::GetFontSize();
        if ((ImGui::Button("Rename", ImVec2(em * 6.0f, 0.0f)) || entered) && exists) {
            const std::string name = d.text;
            const int other = findMarkNamed(choreography, name);
            if (!validMarkName(name)) {
                d.error = "It needs two characters.";
                r.refused = true;
            } else if (other >= 0 && other != d.mark) {
                d.error = "Another mark is called " + name + ".";
                r.refused = true;
            } else {
                r.rename = true;
                r.mark = d.mark;
                r.name = name;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return r;
}

LoiterRequest drawLoiterPopup(LoiterPopup& p, const Choreography& choreography, int cycle, ColorVisionMode colorVision) {
    LoiterRequest r;
    const ImGuiIO& io = ImGui::GetIO();
    auto markNameOf = [&](int m) {
        return m >= 0 && m < static_cast<int>(choreography.marks.size()) ? choreography.marks[static_cast<size_t>(m)].name
                                                                         : std::string("?");
    };
    if (p.openRequested) {
        p.openRequested = false;
        if (cycle > 0) ImGui::OpenPopup("##loiter");
    }
    ImGui::SetNextWindowPos(ImVec2(p.at.x + 16.0f, p.at.y + 16.0f), ImGuiCond_Appearing);
    const bool flashing = ImGui::GetTime() < p.errorUntil;
    const ImU32 errorColor = errorTextColor(colorVision);
    if (flashing) {
        ImGui::PushStyleColor(ImGuiCol_Border, errorColor);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 3.0f);
    }
    p.open = ImGui::BeginPopup("##loiter");
    if (flashing) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (p.open) {
        const int j = p.juggler;
        const int b = p.arrival.beat;
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
            if (choreography.repeats() > 1 && b + n >= cycle) {
                if (blockingBeat) *blockingBeat = -2;
                return false;
            }
            if (j >= 0 && j < static_cast<int>(choreography.keys.size()))
                for (const Keyframe& k : choreography.keys[static_cast<size_t>(j)]) {
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
                p.error = blocking == -2 ? "Too long: it would run past beat " + std::to_string(cycle) +
                                                    ", into the walking loop's copies."
                               : blocking < 0 ? "Too long: the choreography is only " + std::to_string(cycle) + " beats."
                                            : "No room: J" + std::to_string(j + 1) + " has a keyframe " +
                                                  std::to_string(blocking) + " beats later.";
                p.errorUntil = ImGui::GetTime() + 1.5;
                return;
            }
            if (n > 0) {
                Keyframe stay = p.arrival;
                stay.beat = (b + n) % cycle;
                stay.longTurn = false;
                r.loiter = true;
                r.juggler = j;
                r.stay = stay;
            }
            ImGui::CloseCurrentPopup();
        };
        // Loiter lengths already in the choreography, nearest in time to this beat first
        // (beyond the 0-4 buttons); up to four, shortest first.
        std::vector<std::pair<int, int>> used;  // (distance in time, length)
        for (size_t jj = 0; jj < choreography.keys.size(); ++jj) {
            const std::vector<Keyframe>& keys = choreography.keys[jj];
            for (size_t i = 0; i < keys.size() && keys.size() > 1; ++i) {
                const Keyframe& k = keys[i];
                const Keyframe& next = keys[(i + 1) % keys.size()];
                const int gap = ((next.beat - k.beat) % cycle + cycle) % cycle;
                if (gap <= 4 || !sameKeyframeSpot(choreography, k, next)) continue;
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
            p.arrival.mark >= 0 ? "on mark " + markNameOf(p.arrival.mark) : "here";
        ImGui::Text("J%d arrives on beat %ld. Loiter %s for how many beats?", j + 1, p.onBeat + 1,
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
        const bool entered = ImGui::InputText("beats", p.text, sizeof(p.text),
                                              ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        const bool typing = ImGui::IsItemActive();
        ImGui::SameLine();
        if ((ImGui::Button("OK") || entered) && p.text[0] != '\0') apply(std::atoi(p.text));
        ImGui::TextDisabled("Or press a key: 0-9, a = 10, b = 11 ... (as in siteswap). Esc: just arrive.");
        if (ImGui::GetTime() < p.errorUntil)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorColor), "%s", p.error.c_str());
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
    return r;
}
