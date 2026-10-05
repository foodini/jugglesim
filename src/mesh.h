// mesh.h - procedurally generated primitive meshes (position + normal), uploaded to the GPU.
#pragma once

#include "gl_funcs.h"

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

// Unit sphere (radius 1) centered at the origin.
Mesh makeSphereMesh(int slices, int stacks);

// Capped frustum along +Y from y=0 (radius bottomRadius) to y=1 (radius topRadius).
// makeFrustumMesh(1, 1, n) is a unit cylinder.
Mesh makeFrustumMesh(float bottomRadius, float topRadius, int slices);

// Unit cube centered at the origin (extends from -0.5 to 0.5 on each axis).
Mesh makeCubeMesh();

void destroyMesh(Mesh& mesh);

// The fixed set of primitives everything in the scene is built from.
struct Primitives {
    Mesh sphere;
    Mesh cylinder;
    Mesh torso;  // inverted frustum: narrow at the waist (y=0), wide at the shoulders (y=1)
    Mesh cube;

    void create();
    void destroy();
};
