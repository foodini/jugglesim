// mesh.cpp - see mesh.h.
#include "mesh.h"

#include "math3d.h"

#include <cstdint>
#include <vector>

namespace {

struct MeshBuilder {
    std::vector<float> vertices;  // interleaved: px, py, pz, nx, ny, nz
    std::vector<std::uint32_t> indices;

    std::uint32_t addVertex(Vec3 p, Vec3 n) {
        std::uint32_t index = static_cast<std::uint32_t>(vertices.size() / 6);
        vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        return index;
    }

    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        indices.insert(indices.end(), {a, b, c});
    }

    Mesh upload() const {
        Mesh mesh;
        gl::GenVertexArrays(1, &mesh.vao);
        gl::GenBuffers(1, &mesh.vbo);
        gl::GenBuffers(1, &mesh.ebo);

        gl::BindVertexArray(mesh.vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                       vertices.data(), GL_STATIC_DRAW);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                       indices.data(), GL_STATIC_DRAW);

        const GLsizei stride = 6 * sizeof(float);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        gl::EnableVertexAttribArray(1);
        gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                                reinterpret_cast<void*>(3 * sizeof(float)));
        gl::BindVertexArray(0);

        mesh.indexCount = static_cast<GLsizei>(indices.size());
        return mesh;
    }
};

}  // namespace

Mesh makeSphereMesh(int slices, int stacks) {
    MeshBuilder b;
    for (int i = 0; i <= stacks; ++i) {
        float phi = kPi * static_cast<float>(i) / stacks;  // 0 at top, pi at bottom
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * kPi * static_cast<float>(j) / slices;
            Vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            b.addVertex(n, n);
        }
    }
    const int row = slices + 1;
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            std::uint32_t a = i * row + j, c = (i + 1) * row + j;
            b.addTriangle(a, c, a + 1);
            b.addTriangle(a + 1, c, c + 1);
        }
    return b.upload();
}

Mesh makeFrustumMesh(float bottomRadius, float topRadius, int slices) {
    MeshBuilder b;
    // Side. Outward normal of r(y) = rb + (rt - rb) * y is proportional to (cos, rb - rt, sin).
    const float ny = bottomRadius - topRadius;
    for (int j = 0; j <= slices; ++j) {
        float theta = 2.0f * kPi * static_cast<float>(j) / slices;
        float c = std::cos(theta), s = std::sin(theta);
        Vec3 n = normalize(Vec3(c, ny, s));
        b.addVertex(Vec3(c * bottomRadius, 0.0f, s * bottomRadius), n);
        b.addVertex(Vec3(c * topRadius, 1.0f, s * topRadius), n);
    }
    for (int j = 0; j < slices; ++j) {
        std::uint32_t a = 2 * j;
        b.addTriangle(a, a + 1, a + 2);
        b.addTriangle(a + 2, a + 1, a + 3);
    }
    // Caps.
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap == 0 ? 0.0f : 1.0f;
        float r = cap == 0 ? bottomRadius : topRadius;
        Vec3 n(0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f);
        std::uint32_t center = b.addVertex(Vec3(0.0f, y, 0.0f), n);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * kPi * static_cast<float>(j) / slices;
            b.addVertex(Vec3(std::cos(theta) * r, y, std::sin(theta) * r), n);
        }
        for (int j = 0; j < slices; ++j) b.addTriangle(center, center + 1 + j, center + 2 + j);
    }
    return b.upload();
}

Mesh makeCubeMesh() {
    MeshBuilder b;
    const Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vec3& n : normals) {
        // Two axes spanning the face.
        Vec3 u = std::fabs(n.y) > 0.5f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        Vec3 v = cross(n, u);
        Vec3 c = n * 0.5f;
        std::uint32_t base = b.addVertex(c - u * 0.5f - v * 0.5f, n);
        b.addVertex(c + u * 0.5f - v * 0.5f, n);
        b.addVertex(c + u * 0.5f + v * 0.5f, n);
        b.addVertex(c - u * 0.5f + v * 0.5f, n);
        b.addTriangle(base, base + 1, base + 2);
        b.addTriangle(base, base + 2, base + 3);
    }
    return b.upload();
}

void destroyMesh(Mesh& mesh) {
    if (mesh.ebo) gl::DeleteBuffers(1, &mesh.ebo);
    if (mesh.vbo) gl::DeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) gl::DeleteVertexArrays(1, &mesh.vao);
    mesh = Mesh{};
}

void Primitives::create() {
    sphere = makeSphereMesh(32, 16);
    cylinder = makeFrustumMesh(1.0f, 1.0f, 24);
    torso = makeFrustumMesh(0.5f, 1.0f, 32);
    cube = makeCubeMesh();
}

void Primitives::destroy() {
    destroyMesh(sphere);
    destroyMesh(cylinder);
    destroyMesh(torso);
    destroyMesh(cube);
}
