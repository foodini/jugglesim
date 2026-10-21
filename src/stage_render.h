// stage_render.h - drawing the 3D view itself: the jugglers and their props (and flames), and a
// walking ghost.
#pragma once

#include "camera.h"
#include "color_vision.h"
#include "juggle_sim.h"
#include "juggler_figure.h"
#include "mesh.h"
#include "prop_figure.h"
#include "renderer.h"
#include "imgui.h"

#include <vector>

// A ghost of a juggler (the Walk Like... window's preview: where a choice would put them),
// drawn over everything so it shows even inside another juggler: a see-through body, ripples
// spreading out from its feet, a soft pulsing halo, sparks rising, and the path it would walk.
struct WalkGhost {
    bool show = false;
    JugglerPose pose;
    Mat4 placement;
    std::vector<Vec3> path;  // on the floor, round the whole choreography
};

void renderWalkGhost(Renderer& renderer, const Primitives& prims, const WalkGhost& ghost, const CameraView& camera,
                     const GLRect& rect, double time);

// Draws the jugglers and their props into `rect` as `camera` sees them, `selected` juggler
// highlighted. firelight: a dark stage lit by the torches' flames (and in October, pumpkins).
void renderJugglerView(Renderer& renderer, const Primitives& prims, const PropMeshes& propMeshes,
                       const JugglerScene& scene, const CameraView& camera, int selected,
                       ColorVisionMode colorVision, const std::vector<int>& styleOfBall, const GLRect& rect,
                       bool firelight);
