// renderer.cpp - see renderer.h.
#include "renderer.h"

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

}  // namespace

bool Renderer::init(std::string* error) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader, error);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader, error);
    if (!vs || !fs) {
        if (vs) gl::DeleteShader(vs);
        if (fs) gl::DeleteShader(fs);
        return false;
    }

    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    GLint ok = 0;
    gl::GetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl::GetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? static_cast<size_t>(len) : 1, '\0');
        gl::GetProgramInfoLog(program_, len, nullptr, &log[0]);
        if (error) *error += "Program link:\n" + log;
        gl::DeleteProgram(program_);
        program_ = 0;
        return false;
    }

    locModel_ = gl::GetUniformLocation(program_, "uModel");
    locViewProj_ = gl::GetUniformLocation(program_, "uViewProj");
    locColor_ = gl::GetUniformLocation(program_, "uColor");
    locLightDir_ = gl::GetUniformLocation(program_, "uLightDir");
    return true;
}

void Renderer::shutdown() {
    if (program_) gl::DeleteProgram(program_);
    program_ = 0;
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
