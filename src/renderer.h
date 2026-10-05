// renderer.h - a single lit, solid-color shader for drawing primitive meshes.
#pragma once

#include "gl_funcs.h"
#include "math3d.h"
#include "mesh.h"

#include <string>

class Renderer {
public:
    bool init(std::string* error);
    void shutdown();

    // lightDir is the direction light travels (pointing from the light into the scene).
    void beginScene(const Mat4& viewProj, Vec3 lightDir);
    void drawMesh(const Mesh& mesh, const Mat4& model, Vec3 color);
    void endScene();

private:
    GLuint program_ = 0;
    GLint locModel_ = -1;
    GLint locViewProj_ = -1;
    GLint locColor_ = -1;
    GLint locLightDir_ = -1;
};
