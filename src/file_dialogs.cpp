// file_dialogs.cpp - see file_dialogs.h.

#include "file_dialogs.h"

#include "ladder_edit.h"
#include "panels.h"
#include "siteswap.h"

#include <algorithm>
#include <cstdio>
#include <string>

SaveChangesChoice drawSaveChangesDialog(SavePrompt& p, const std::string& name, const char* also, bool patternIsOpen) {
    SaveChangesChoice choice = SaveChangesChoice::None;
    if (p.requested) {
        ImGui::OpenPopup("Save changes?");
        p.requested = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    p.open = ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (p.open) {
        const float em = ImGui::GetFontSize();
        ImGui::PushTextWrapPos(em * 34.0f);
        ImGui::Text("Save %s%s to My Patterns before %s?", name.c_str(), also, p.forQuit ? "quitting" : "starting a new pattern");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(patternIsOpen ? "Save" : "Save...", ImVec2(em * 7.0f, 0.0f))) {
            choice = SaveChangesChoice::Save;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(em * 7.0f, 0.0f))) {
            choice = SaveChangesChoice::DontSave;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return choice;
}

bool drawNewPatternDialog(NewPatternDialog& d) {
    bool create = false;
    if (d.openRequested) {
        ImGui::OpenPopup("New Pattern");
        d.openRequested = false;
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
            ImGui::RadioButton(label, &d.jugglers, n);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Period");
        ImGui::SameLine(em * 5.0f);
        ImGui::SetNextItemWidth(em * 8.0f);
        ImGui::InputInt("##period", &d.period);
        d.period = std::clamp(d.period, 1, kMaxLoopBeats);
        ImGui::SameLine();
        ImGui::TextDisabled("beats");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Count");
        ImGui::SameLine(em * 5.0f);
        ImGui::SetNextItemWidth(em * 8.0f);
        const std::string countName = d.count >= 2
                                          ? std::to_string(d.count) + "-count"
                                          : std::string("None");
        if (ImGui::BeginCombo("##count", countName.c_str())) {
            if (ImGui::Selectable("None", d.count < 2)) d.count = 0;
            for (int n = 2; n <= 8; ++n)
                if (ImGui::Selectable((std::to_string(n) + "-count").c_str(), d.count == n))
                    d.count = n;
            ImGui::EndCombo();
        }
        if (d.count >= 2) {
            ImGui::SameLine();
            ImGui::TextDisabled("(the period becomes a multiple of %d)", d.count);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Walking");
        ImGui::SameLine(em * 5.0f);
        ImGui::SetNextItemWidth(em * 8.0f);
        ImGui::InputInt("##walking", &d.walking);
        d.walking = std::clamp(d.walking, 0, kMaxChoreographyLength);
        ImGui::SameLine();
        ImGui::TextDisabled(d.walking == 0 ? "beats (0: no choreography yet)" : "beats");
        if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("The choreography's own period: the beats of walking written out before its\n"
                              "loop (a short passing pattern can have a long walk, and the other way round).");
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(em * 6.0f, 0.0f))) {
            create = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return create;
}

void drawFileErrorDialog(std::string& error) {
    if (!error.empty()) ImGui::OpenPopup("Couldn't save");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Couldn't save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(error.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
            error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
