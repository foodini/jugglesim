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

Mesh makeTorusSectorsMesh(float majorRadius, float minorRadius, const float* startAngles, const float* endAngles,
                          int count) {
    MeshBuilder b;
    const int around = 16;  // round the tube
    for (int k = 0; k < count; ++k) {
        const float a0 = startAngles[k], a1 = endAngles[k];
        const int steps = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (2.0f * kPi) * 128.0f)));
        const std::uint32_t first = static_cast<std::uint32_t>(b.vertices.size() / 6);
        for (int j = 0; j <= steps; ++j) {
            const float theta = a0 + (a1 - a0) * static_cast<float>(j) / steps;
            const Vec3 radial(std::cos(theta), 0.0f, std::sin(theta));
            for (int i = 0; i <= around; ++i) {
                const float phi = 2.0f * kPi * static_cast<float>(i) / around;
                const Vec3 n = radial * std::cos(phi) + Vec3(0.0f, std::sin(phi), 0.0f);
                b.addVertex(radial * majorRadius + n * minorRadius, n);
            }
        }
        const std::uint32_t row = static_cast<std::uint32_t>(around + 1);
        for (int j = 0; j < steps; ++j)
            for (int i = 0; i < around; ++i) {
                const std::uint32_t a = first + static_cast<std::uint32_t>(j) * row + static_cast<std::uint32_t>(i);
                b.addTriangle(a, a + 1, a + row);
                b.addTriangle(a + row, a + 1, a + row + 1);
            }
        // Close the ends (a disc across the tube), so a piece isn't hollow.
        for (int end = 0; end < 2; ++end) {
            const float theta = end == 0 ? a0 : a1;
            const Vec3 radial(std::cos(theta), 0.0f, std::sin(theta));
            const Vec3 n = end == 0 ? Vec3(std::sin(theta), 0.0f, -std::cos(theta)) : Vec3(-std::sin(theta), 0.0f, std::cos(theta));
            const std::uint32_t center = b.addVertex(radial * majorRadius, n);
            for (int i = 0; i <= around; ++i) {
                const float phi = 2.0f * kPi * static_cast<float>(i) / around;
                b.addVertex(radial * (majorRadius + minorRadius * std::cos(phi)) + Vec3(0.0f, minorRadius * std::sin(phi), 0.0f), n);
            }
            for (int i = 0; i < around; ++i) {
                const std::uint32_t a = center + 1 + static_cast<std::uint32_t>(i);
                if (end == 0) b.addTriangle(center, a + 1, a);
                else b.addTriangle(center, a, a + 1);
            }
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

namespace {

// The pumpkin's shape: an ellipsoid (kPumpkinWide across, kPumpkinTall high), ribbed, dimpled
// top and bottom.
constexpr float kPumpkinWide = 0.135f, kPumpkinTall = 0.112f;
constexpr int kPumpkinRibs = 8;

Vec3 pumpkinPoint(float phi, float theta) {  // phi 0 at the top to pi at the bottom
    const float rib = 1.0f - 0.045f * (0.5f - 0.5f * std::cos(kPumpkinRibs * theta));
    const float dimple = 1.0f - 0.18f * (std::exp(-phi * phi * 10.0f) + std::exp(-(kPi - phi) * (kPi - phi) * 10.0f));
    return Vec3(std::sin(phi) * std::cos(theta) * kPumpkinWide * rib, std::cos(phi) * kPumpkinTall * dimple,
                std::sin(phi) * std::sin(theta) * kPumpkinWide * rib);
}

Mesh makePumpkinMesh() {
    MeshBuilder b;
    const int stacks = 20, slices = 64;
    const float e = 1e-3f;
    for (int i = 0; i <= stacks; ++i) {
        const float phi = std::clamp(kPi * static_cast<float>(i) / stacks, 2e-3f, kPi - 2e-3f);
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / slices;
            const Vec3 p = pumpkinPoint(phi, theta);
            Vec3 n = cross(pumpkinPoint(phi, theta + e) - pumpkinPoint(phi, theta - e),
                           pumpkinPoint(phi + e, theta) - pumpkinPoint(phi - e, theta));
            if (dot(n, p) < 0.0f) n = n * -1.0f;
            b.addVertex(p, normalize(n));
        }
    }
    const int row = slices + 1;
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const std::uint32_t a = static_cast<std::uint32_t>(i * row + j), c = static_cast<std::uint32_t>((i + 1) * row + j);
            b.addTriangle(a, c, a + 1);
            b.addTriangle(a + 1, c, c + 1);
        }
    return b.upload();
}

// The carved face, in the plane of the face (x across, y up), each point then laid on the
// pumpkin's surface (a little proud of it).
Mesh makePumpkinFaceMesh() {
    MeshBuilder b;
    auto onSurface = [](float x, float y) {
        const float u = (x / kPumpkinWide) * (x / kPumpkinWide) + (y / kPumpkinTall) * (y / kPumpkinTall);
        return Vec3(x, y, kPumpkinWide * std::sqrt(std::max(0.0f, 1.0f - u)) + 0.004f);
    };
    auto triangle = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
        const Vec3 n(0.0f, 0.0f, 1.0f);
        const std::uint32_t a = b.addVertex(onSurface(x0, y0), n);
        b.addVertex(onSurface(x1, y1), n);
        b.addVertex(onSurface(x2, y2), n);
        b.addTriangle(a, a + 1, a + 2);
    };
    // Eyes (triangles, points up) and a nose.
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        triangle(s * 0.062f, 0.008f, s * 0.020f, 0.008f, s * 0.041f, 0.050f);
    }
    triangle(-0.013f, -0.014f, 0.013f, -0.014f, 0.0f, 0.010f);
    // A grin: between an upper and a lower curve, smiling up at the ends, with two teeth down
    // from the top and one up from the bottom.
    const int n = 25;
    const float half = 0.078f;
    std::vector<float> xs(n), top(n), bottom(n);
    for (int i = 0; i < n; ++i) {
        const float x = -half + 2.0f * half * static_cast<float>(i) / (n - 1);
        const float u = (x / half) * (x / half);
        float yt = -0.030f + 0.026f * u, yb = -0.062f + 0.042f * u;
        if (std::fabs(std::fabs(x) - 0.028f) < 0.008f) yt -= 0.013f;  // upper teeth
        if (std::fabs(x) < 0.008f) yb += 0.013f;                        // a lower tooth
        xs[static_cast<size_t>(i)] = x;
        top[static_cast<size_t>(i)] = yt;
        bottom[static_cast<size_t>(i)] = std::min(yb, yt - 0.002f);
    }
    for (int i = 0; i + 1 < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        triangle(xs[k], top[k], xs[k], bottom[k], xs[k + 1], top[k + 1]);
        triangle(xs[k + 1], top[k + 1], xs[k], bottom[k], xs[k + 1], bottom[k + 1]);
    }
    return b.upload();
}

}  // namespace

void Primitives::create() {
    sphere = makeSphereMesh(32, 16);
    cylinder = makeFrustumMesh(1.0f, 1.0f, 24);
    torso = makeFrustumMesh(0.5f, 1.0f, 32);
    cube = makeCubeMesh();
    pumpkin = makePumpkinMesh();
    pumpkinFace = makePumpkinFaceMesh();
}

void Primitives::destroy() {
    destroyMesh(sphere);
    destroyMesh(cylinder);
    destroyMesh(torso);
    destroyMesh(cube);
    destroyMesh(pumpkin);
    destroyMesh(pumpkinFace);
}
