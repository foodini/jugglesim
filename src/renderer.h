// renderer.h - a single lit, solid-color shader for drawing primitive meshes.
#pragma once

#include "gl_funcs.h"
#include "math3d.h"
#include "mesh.h"

#include <string>
#include <vector>

// One vertex of glowing, unlit geometry (trails): position and RGBA color.
struct GlowVertex {
    float x, y, z;
    float r, g, b, a;
};

// Appends a camera-facing ribbon along `points` (a polyline) to `out` as triangles. fade[i]
// (0-1) scales the alpha at each point; width is in meters. dashLengths/dashCount give an
// on/off dash pattern along the ribbon in units of dashUnit meters (dashCount 0 = solid).
void appendRibbon(std::vector<GlowVertex>& out, const std::vector<Vec3>& points,
                  const std::vector<float>& fade, Vec3 color, float alpha, float width,
                  Vec3 cameraPos, const float* dashLengths, int dashCount, float dashUnit);

class Renderer {
public:
    bool init(std::string* error);
    void shutdown();

    // lightDir is the direction light travels (pointing from the light into the scene).
    void beginScene(const Mat4& viewProj, Vec3 lightDir);
    void drawMesh(const Mesh& mesh, const Mat4& model, Vec3 color);
    void endScene();

    // Draws glowing triangles with additive blending: depth-tested against the scene but not
    // writing depth, so overlapping glows add up. Call after the solid geometry.
    void drawGlow(const Mat4& viewProj, const std::vector<GlowVertex>& triangles);
    // Draws overlay triangles (lines and marks on the floor) with ordinary alpha blending:
    // depth-tested against the scene, pulled a little toward the camera so the floor they lie
    // on doesn't hide them, and not writing depth. Later triangles draw over earlier ones.
    void drawOverlay(const Mat4& viewProj, const std::vector<GlowVertex>& triangles);

private:
    GLuint program_ = 0;
    GLint locModel_ = -1;
    GLint locViewProj_ = -1;
    GLint locColor_ = -1;
    GLint locLightDir_ = -1;

    GLuint glowProgram_ = 0;
    GLint locGlowViewProj_ = -1;
    GLuint glowVao_ = 0;
    GLuint glowVbo_ = 0;
};
