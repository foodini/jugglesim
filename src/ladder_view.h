// ladder_view.h - ladder diagram of a pattern, drawn with ImGui's draw list.
//
// Time runs left to right. The top rail is the right hand, the bottom rail the left hand,
// with beat 1 thrown by the right hand. Each throw is a curve from its throw beat to its
// landing beat, styled (color + marker shape + dash pattern) by which ball it carries.
//
// Read-only for now. The canvas is an ImGui InvisibleButton so later editing can hit-test it.
//
// Above the diagram is a toolbar row for ladder modes and tools. It doesn't change anything
// itself: it returns what the user asked for, and the caller applies it.
#pragma once

#include "color_vision.h"
#include "pattern.h"

// Longest loop the period control will write out.
constexpr int kMaxLoopPeriodBeats = 64;

struct LadderToolbarRequest {
    int newPeriodBeats = 0;  // 0 = no change
};

// Draws the toolbar row (mode toggle, period control). Call before drawLadderDiagram.
LadderToolbarRequest drawLadderToolbar(const Pattern& pattern, bool patternValid);

// Fills the remaining content region of the current ImGui window.
void drawLadderDiagram(const Pattern& pattern, bool patternValid, ColorVisionMode colorVision);
