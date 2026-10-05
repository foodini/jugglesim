// ladder_view.h - ladder diagram of a siteswap, drawn with ImGui's draw list.
//
// Time runs left to right. The top rail is the right hand, the bottom rail the left hand,
// with beat 1 thrown by the right hand. Each throw is a curve from its throw beat to its
// landing beat, styled (color + marker shape + dash pattern) by which ball it carries.
//
// Read-only for now. The canvas is an ImGui InvisibleButton so later editing can hit-test it.
#pragma once

#include "color_vision.h"
#include "siteswap.h"

// Fills the remaining content region of the current ImGui window.
void drawLadderDiagram(const Siteswap& siteswap, ColorVisionMode colorVision);
