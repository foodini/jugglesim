// color_vision.cpp - see color_vision.h.
#include "color_vision.h"

#include "draw_helpers.h"

#include <cstring>
#include <string>

namespace {

constexpr ImU32 rgb(unsigned int hex) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 255);
}

// Palettes in assignment order (see the note in color_vision.h).
// Worst-pair OKLab distance x100 across all 8 colors, under the mode's own simulation:
//   Normal 15.1 (and still >= 10.9 under protan and deutan simulation), Protan 16.4,
//   Deutan 10.9 (17.8 for the first 6), Tritan 13.2.
const ImU32 kNormal[] = {rgb(0x954FEC), rgb(0xEEEF08), rgb(0xE42414), rgb(0xBDC3F8),
                         rgb(0x9489AA), rgb(0xB8BA6D), rgb(0xDAECC9), rgb(0x358660)};
const ImU32 kProtan[] = {rgb(0xB43ECE), rgb(0xD4F73E), rgb(0x37951D), rgb(0x8CC3FC),
                         rgb(0xE11A5E), rgb(0x898CAE), rgb(0xF8B884), rgb(0x95FBE7)};
const ImU32 kDeutan[] = {rgb(0x0569F5), rgb(0xEEEF08), rgb(0x82830E), rgb(0x7FD4FC),
                         rgb(0x5D98B4), rgb(0x7ECB6C), rgb(0x95FFC8), rgb(0xC265FD)};
const ImU32 kTritan[] = {rgb(0xD83462), rgb(0x22FAF1), rgb(0x0E8C41), rgb(0xF9ADD0),
                         rgb(0xB464E8), rgb(0x74C163), rgb(0xD8F37A), rgb(0xEA8241)};
// Monochrome: evenly spaced lightness steps; identity comes from shape and dash.
const ImU32 kMonochrome[] = {rgb(0xF5F5F5), rgb(0x808080), rgb(0xCACACA), rgb(0xA4A4A4)};

struct Palette {
    const ImU32* colors;
    int count;
};

Palette palette(ColorVisionMode mode) {
    switch (mode) {
        case ColorVisionMode::Protan: return {kProtan, 8};
        case ColorVisionMode::Deutan: return {kDeutan, 8};
        case ColorVisionMode::Tritan: return {kTritan, 8};
        case ColorVisionMode::Monochrome: return {kMonochrome, 4};
        default: return {kNormal, 8};
    }
}

}  // namespace

const char* colorVisionModeName(ColorVisionMode mode) {
    switch (mode) {
        case ColorVisionMode::Normal: return "Normal";
        case ColorVisionMode::Protan: return "Protan (red-weak)";
        case ColorVisionMode::Deutan: return "Deutan (green-weak)";
        case ColorVisionMode::Tritan: return "Tritan (blue-weak)";
        case ColorVisionMode::Monochrome: return "Monochrome";
        default: return "?";
    }
}

const char* colorVisionModeKey(ColorVisionMode mode) {
    switch (mode) {
        case ColorVisionMode::Normal: return "normal";
        case ColorVisionMode::Protan: return "protan";
        case ColorVisionMode::Deutan: return "deutan";
        case ColorVisionMode::Tritan: return "tritan";
        case ColorVisionMode::Monochrome: return "monochrome";
        default: return "normal";
    }
}

bool colorVisionModeFromKey(const char* key, ColorVisionMode* mode) {
    for (int i = 0; i < static_cast<int>(ColorVisionMode::Count); ++i) {
        const ColorVisionMode candidate = static_cast<ColorVisionMode>(i);
        if (std::strcmp(key, colorVisionModeKey(candidate)) == 0) {
            *mode = candidate;
            return true;
        }
    }
    return false;
}

BallStyle ballStyle(ColorVisionMode mode, int ballIndex) {
    if (ballIndex < 0) ballIndex = 0;
    const Palette p = palette(mode);
    const int shapeCount = static_cast<int>(MarkerShape::Count);
    const int dashCount = static_cast<int>(DashPattern::Count);
    BallStyle style;
    // A palette no longer than the dash cycle (Monochrome's 4 grays vs 4 dashes) would change
    // in lockstep with the dash, wasting a channel; skew it by one step per cycle instead.
    const int colorIndex = p.count <= dashCount ? (ballIndex + ballIndex / p.count) % p.count
                                                : ballIndex % p.count;
    style.color = p.colors[colorIndex];
    style.shape = static_cast<MarkerShape>(ballIndex % shapeCount);
    // Shape and dash cycle with coprime-ish periods (6 and 4), so the (shape, dash) pair is
    // unique for the first 12 balls even with no color at all.
    style.dash = static_cast<DashPattern>(ballIndex % dashCount);
    return style;
}

void dashPatternLengths(DashPattern dash, const float** lengths, int* count) {
    static const float kLong[] = {4.0f, 2.0f};
    static const float kShort[] = {1.5f, 1.5f};
    static const float kDashDot[] = {4.0f, 1.5f, 0.7f, 1.5f};
    switch (dash) {
        case DashPattern::LongDash: *lengths = kLong; *count = 2; return;
        case DashPattern::ShortDash: *lengths = kShort; *count = 2; return;
        case DashPattern::DashDot: *lengths = kDashDot; *count = 4; return;
        default: *lengths = nullptr; *count = 0; return;
    }
}

ImU32 errorTextColor(ColorVisionMode mode) {
    switch (mode) {
        case ColorVisionMode::Protan:
        case ColorVisionMode::Deutan: return rgb(0xFFB000);  // amber: reds read dark/muddy
        case ColorVisionMode::Monochrome: return rgb(0xFFFFFF);
        default: return rgb(0xFF7A6E);
    }
}

void drawColorVisionPreview(bool* open, ColorVisionMode current) {
    if (!ImGui::Begin("Color vision preview", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Each ball has a color, a marker shape and a dash pattern.");
    ImGui::TextUnformatted("Pick the mode under View > Color Vision.");
    ImGui::Spacing();

    const int kBalls = 8;
    const float fontSize = ImGui::GetFontSize();
    float labelWidth = 0.0f;
    for (int m = 0; m < static_cast<int>(ColorVisionMode::Count); ++m) {
        const float w =
            ImGui::CalcTextSize(colorVisionModeName(static_cast<ColorVisionMode>(m))).x;
        if (w > labelWidth) labelWidth = w;
    }
    labelWidth += fontSize * 2.0f;
    const float cellWidth = fontSize * 4.5f;
    const float rowHeight = fontSize * 2.2f;
    const int rows = static_cast<int>(ColorVisionMode::Count) + 1;  // + header row
    const ImVec2 size(labelWidth + cellWidth * kBalls + fontSize, rowHeight * rows);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kCanvasBackground);

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    // Header: ball numbers.
    for (int b = 0; b < kBalls; ++b) {
        const std::string label = "Ball " + std::to_string(b + 1);
        const float x = origin.x + labelWidth + cellWidth * b;
        dl->AddText(ImVec2(x, origin.y + (rowHeight - fontSize) * 0.5f), dimColor, label.c_str());
    }

    for (int m = 0; m < static_cast<int>(ColorVisionMode::Count); ++m) {
        const ColorVisionMode mode = static_cast<ColorVisionMode>(m);
        const float rowTop = origin.y + rowHeight * (m + 1);
        const float midY = rowTop + rowHeight * 0.5f;
        const bool isCurrent = mode == current;
        const std::string label = std::string(isCurrent ? "> " : "  ") + colorVisionModeName(mode);
        dl->AddText(ImVec2(origin.x + fontSize * 0.5f, midY - fontSize * 0.5f),
                    isCurrent ? textColor : dimColor, label.c_str());

        for (int b = 0; b < kBalls; ++b) {
            const BallStyle style = ballStyle(mode, b);
            const float x0 = origin.x + labelWidth + cellWidth * b;
            const ImVec2 start(x0 + fontSize * 0.4f, midY);
            const ImVec2 end(x0 + cellWidth - fontSize * 0.8f, midY);
            drawStyledLine(dl, start, end, style.color, 2.5f, style.dash, fontSize * 0.35f);
            drawArrowHead(dl, end, start, style.color, fontSize * 0.55f);
            drawMarker(dl, start, fontSize * 0.36f, style.shape, style.color, 2.0f,
                       kCanvasBackground);
        }
    }
    ImGui::End();
}
