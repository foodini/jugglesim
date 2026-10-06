// mesh.cpp - see mesh.h.
#include "mesh.h"

#include "math3d.h"

#include <algorithm>
#include <cmath>
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

Mesh makeLatheMesh(const float* ys, const float* radii, int count, int slices) {
    MeshBuilder b;
    // Normal of each profile point in the (radial, y) plane: the average of the normals of the
    // segments on either side (a segment going up by dy while the radius changes by dr has
    // outward normal (dy, -dr)).
    std::vector<float> nr(static_cast<size_t>(count)), ny(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        float sumR = 0.0f, sumY = 0.0f;
        for (int k = i - 1; k <= i; ++k) {
            if (k < 0 || k + 1 >= count) continue;
            const float dy = ys[k + 1] - ys[k], dr = radii[k + 1] - radii[k];
            const float len = std::sqrt(dy * dy + dr * dr);
            if (len < 1e-9f) continue;
            sumR += dy / len;
            sumY += -dr / len;
        }
        const float len = std::sqrt(sumR * sumR + sumY * sumY);
        nr[static_cast<size_t>(i)] = len > 1e-9f ? sumR / len : 1.0f;
        ny[static_cast<size_t>(i)] = len > 1e-9f ? sumY / len : 0.0f;
    }
    const std::uint32_t row = static_cast<std::uint32_t>(slices + 1);
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / slices;
            const float c = std::cos(theta), s = std::sin(theta);
            const size_t k = static_cast<size_t>(i);
            b.addVertex(Vec3(c * radii[i], ys[i], s * radii[i]), Vec3(c * nr[k], ny[k], s * nr[k]));
        }
    }
    for (int i = 0; i + 1 < count; ++i)
        for (int j = 0; j < slices; ++j) {
            const std::uint32_t a = static_cast<std::uint32_t>(i) * row + static_cast<std::uint32_t>(j);
            const std::uint32_t c = a + row;
            b.addTriangle(a, c, a + 1);
            b.addTriangle(a + 1, c, c + 1);
        }
    for (int end = 0; end < 2; ++end) {
        const int i = end == 0 ? 0 : count - 1;
        if (radii[i] <= 0.0f) continue;
        const Vec3 n(0.0f, end == 0 ? -1.0f : 1.0f, 0.0f);
        const std::uint32_t center = b.addVertex(Vec3(0.0f, ys[i], 0.0f), n);
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / slices;
            b.addVertex(Vec3(std::cos(theta) * radii[i], ys[i], std::sin(theta) * radii[i]), n);
        }
        for (int j = 0; j < slices; ++j)
            b.addTriangle(center, center + 1 + static_cast<std::uint32_t>(j), center + 2 + static_cast<std::uint32_t>(j));
    }
    return b.upload();
}

Mesh makeRingSectorsMesh(float innerRadius, float outerRadius, float halfThickness,
                         const float* startAngles, const float* endAngles, int count) {
    MeshBuilder b;
    for (int k = 0; k < count; ++k) {
        const float a0 = startAngles[k], a1 = endAngles[k];
        const int steps = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (2.0f * kPi) * 96.0f)));
        // Four faces: top, bottom (flat, normal +-Y), outer and inner rims (normal radial).
        for (int face = 0; face < 4; ++face) {
            const std::uint32_t first = static_cast<std::uint32_t>(b.vertices.size() / 6);
            for (int j = 0; j <= steps; ++j) {
                const float theta = a0 + (a1 - a0) * static_cast<float>(j) / steps;
                const float c = std::cos(theta), s = std::sin(theta);
                const Vec3 radial(c, 0.0f, s);
                if (face < 2) {
                    const float y = face == 0 ? halfThickness : -halfThickness;
                    const Vec3 n(0.0f, face == 0 ? 1.0f : -1.0f, 0.0f);
                    b.addVertex(Vec3(c * innerRadius, y, s * innerRadius), n);
                    b.addVertex(Vec3(c * outerRadius, y, s * outerRadius), n);
                } else {
                    const float r = face == 2 ? outerRadius : innerRadius;
                    const Vec3 n = face == 2 ? radial : -radial;
                    b.addVertex(Vec3(c * r, -halfThickness, s * r), n);
                    b.addVertex(Vec3(c * r, halfThickness, s * r), n);
                }
            }
            for (int j = 0; j < steps; ++j) {
                const std::uint32_t a = first + 2 * static_cast<std::uint32_t>(j);
                b.addTriangle(a, a + 1, a + 2);
                b.addTriangle(a + 2, a + 1, a + 3);
            }
        }
        // Close the sector's ends, so a gap between sectors (or a single sector) isn't hollow.
        for (int end = 0; end < 2; ++end) {
            const float theta = end == 0 ? a0 : a1;
            const float c = std::cos(theta), s = std::sin(theta);
            const Vec3 n = end == 0 ? Vec3(s, 0.0f, -c) : Vec3(-s, 0.0f, c);
            const std::uint32_t first = b.addVertex(Vec3(c * innerRadius, -halfThickness, s * innerRadius), n);
            b.addVertex(Vec3(c * outerRadius, -halfThickness, s * outerRadius), n);
            b.addVertex(Vec3(c * innerRadius, halfThickness, s * innerRadius), n);
            b.addVertex(Vec3(c * outerRadius, halfThickness, s * outerRadius), n);
            b.addTriangle(first, first + 1, first + 2);
            b.addTriangle(first + 2, first + 1, first + 3);
        }
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
