// renderer.cpp - see renderer.h.
#include "renderer.h"

#include <algorithm>

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    // Inverse-transpose so non-uniformly scaled primitives still get correct normals.
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uLightDir;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    float key = max(dot(n, -uLightDir), 0.0);
    float fill = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0) * 0.2;  // soft light from the camera side
    vec3 c = uColor * (0.28 + 0.65 * key + fill);
    fragColor = vec4(c, 1.0);
}
)";

const char* kGlowVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kGlowFragmentShader = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

GLuint compileShader(GLenum type, const char* source, std::string* error) {
    GLuint shader = gl::CreateShader(type);
    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);
    GLint ok = 0;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? static_cast<size_t>(len) : 1, '\0');
        gl::GetShaderInfoLog(shader, len, nullptr, &log[0]);
        if (error)
            *error += (type == GL_VERTEX_SHADER ? "Vertex shader:\n" : "Fragment shader:\n") + log;
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

// Compiles and links a vertex + fragment shader pair. Returns 0 (and appends to *error) on failure.
GLuint buildProgram(const char* vertexSource, const char* fragmentSource, std::string* error) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, error);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (!vs || !fs) {
        if (vs) gl::DeleteShader(vs);
        if (fs) gl::DeleteShader(fs);
        return 0;
    }
    GLuint program = gl::CreateProgram();
    gl::AttachShader(program, vs);
    gl::AttachShader(program, fs);
    gl::LinkProgram(program);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    GLint ok = 0;
    gl::GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? static_cast<size_t>(len) : 1, '\0');
        gl::GetProgramInfoLog(program, len, nullptr, &log[0]);
        if (error) *error += "Program link:\n" + log;
        gl::DeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace

bool Renderer::init(std::string* error) {
    program_ = buildProgram(kVertexShader, kFragmentShader, error);
    if (!program_) return false;
    locModel_ = gl::GetUniformLocation(program_, "uModel");
    locViewProj_ = gl::GetUniformLocation(program_, "uViewProj");
    locColor_ = gl::GetUniformLocation(program_, "uColor");
    locLightDir_ = gl::GetUniformLocation(program_, "uLightDir");

    glowProgram_ = buildProgram(kGlowVertexShader, kGlowFragmentShader, error);
    if (!glowProgram_) return false;
    locGlowViewProj_ = gl::GetUniformLocation(glowProgram_, "uViewProj");
    gl::GenVertexArrays(1, &glowVao_);
    gl::GenBuffers(1, &glowVbo_);
    gl::BindVertexArray(glowVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, glowVbo_);
    const GLsizei stride = sizeof(GlowVertex);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<void*>(3 * sizeof(float)));
    gl::BindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    if (glowVbo_) gl::DeleteBuffers(1, &glowVbo_);
    if (glowVao_) gl::DeleteVertexArrays(1, &glowVao_);
    if (glowProgram_) gl::DeleteProgram(glowProgram_);
    if (program_) gl::DeleteProgram(program_);
    glowVbo_ = glowVao_ = glowProgram_ = program_ = 0;
}

void Renderer::drawGlow(const Mat4& viewProj, const std::vector<GlowVertex>& triangles) {
    if (triangles.empty() || !glowProgram_) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive: overlapping glows brighten
    gl::UseProgram(glowProgram_);
    gl::UniformMatrix4fv(locGlowViewProj_, 1, GL_FALSE, viewProj.m);
    gl::BindVertexArray(glowVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, glowVbo_);
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(triangles.size() * sizeof(GlowVertex)),
                   triangles.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangles.size()));
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

void Renderer::drawOverlay(const Mat4& viewProj, const std::vector<GlowVertex>& triangles) {
    if (triangles.empty() || !glowProgram_) return;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -4.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(glowProgram_);
    gl::UniformMatrix4fv(locGlowViewProj_, 1, GL_FALSE, viewProj.m);
    gl::BindVertexArray(glowVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, glowVbo_);
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(triangles.size() * sizeof(GlowVertex)),
                   triangles.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangles.size()));
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

void appendRibbon(std::vector<GlowVertex>& out, const std::vector<Vec3>& points,
                  const std::vector<float>& fade, Vec3 color, float alpha, float width,
                  Vec3 cameraPos, const float* dashLengths, int dashCount, float dashUnit) {
    const size_t n = points.size();
    if (n < 2) return;
    // Half-width offset at each point: perpendicular to both the path and the view direction.
    std::vector<Vec3> side(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec3 prev = points[i > 0 ? i - 1 : i];
        const Vec3 next = points[i + 1 < n ? i + 1 : i];
        Vec3 s = cross(next - prev, cameraPos - points[i]);
        side[i] = normalize(s) * (0.5f * width);
    }
    // Walk the polyline, keeping only the "on" stretches of the dash pattern.
    int element = 0;
    float remaining = dashCount > 0 ? dashLengths[0] * dashUnit : 1e9f;
    bool on = true;
    auto vertex = [&](Vec3 p, Vec3 s, float sign, float f) {
        const float a = alpha * (f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f));
        out.push_back({p.x + s.x * sign, p.y + s.y * sign, p.z + s.z * sign, color.x, color.y, color.z, a});
    };
    auto quad = [&](Vec3 p0, Vec3 s0, float f0, Vec3 p1, Vec3 s1, float f1) {
        vertex(p0, s0, -1.0f, f0); vertex(p0, s0, 1.0f, f0); vertex(p1, s1, 1.0f, f1);
        vertex(p0, s0, -1.0f, f0); vertex(p1, s1, 1.0f, f1); vertex(p1, s1, -1.0f, f1);
    };
    for (size_t i = 0; i + 1 < n; ++i) {
        const Vec3 a = points[i], b = points[i + 1];
        const float segment = length(b - a);
        float used = 0.0f;
        while (used < segment) {
            const float step = std::min(remaining, segment - used);
            const float u0 = used / segment, u1 = (used + step) / segment;
            if (on) {
                quad(a + (b - a) * u0, side[i] + (side[i + 1] - side[i]) * u0, fade[i] + (fade[i + 1] - fade[i]) * u0,
                     a + (b - a) * u1, side[i] + (side[i + 1] - side[i]) * u1, fade[i] + (fade[i + 1] - fade[i]) * u1);
            }
            used += step;
            remaining -= step;
            if (remaining <= 1e-6f && dashCount > 0) {
                on = !on;
                element = (element + 1) % dashCount;
                remaining = dashLengths[element] * dashUnit;
            }
        }
    }
}

void Renderer::beginScene(const Mat4& viewProj, Vec3 lightDir) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    gl::UseProgram(program_);
    gl::UniformMatrix4fv(locViewProj_, 1, GL_FALSE, viewProj.m);
    Vec3 l = normalize(lightDir);
    gl::Uniform3fv(locLightDir_, 1, &l.x);
}

void Renderer::drawMesh(const Mesh& mesh, const Mat4& model, Vec3 color) {
    gl::UniformMatrix4fv(locModel_, 1, GL_FALSE, model.m);
    gl::Uniform3fv(locColor_, 1, &color.x);
    gl::BindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
}

void Renderer::endScene() {
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    glDisable(GL_DEPTH_TEST);
}
