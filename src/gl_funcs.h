// gl_funcs.h - minimal OpenGL 3.3 function loader for JuggleSim.
//
// Windows' opengl32.lib only exports OpenGL 1.1. Everything newer has to be fetched at runtime
// with wglGetProcAddress after a context is current. Rather than pull in GLAD/GLEW, we load
// only the handful of functions we actually use. Add new ones to JS_GL_FUNCTIONS below.
//
// Usage: gl::CreateShader(...), gl::BindVertexArray(...), etc.
// OpenGL 1.1 functions (glClear, glViewport, glDrawElements, ...) are called directly.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <string>

typedef char GLchar;
typedef std::ptrdiff_t GLsizeiptr;

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif

// X-macro list: return type, name (without the "gl" prefix), parameter list.
#define JS_GL_FUNCTIONS(X)                                                                      \
    X(GLuint, CreateShader, (GLenum type))                                                      \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* str,              \
                           const GLint* length))                                                \
    X(void, CompileShader, (GLuint shader))                                                     \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint * params))                         \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei * length, GLchar * log)) \
    X(void, DeleteShader, (GLuint shader))                                                      \
    X(GLuint, CreateProgram, (void))                                                            \
    X(void, AttachShader, (GLuint program, GLuint shader))                                      \
    X(void, LinkProgram, (GLuint program))                                                      \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint * params))                       \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei * length,              \
                                GLchar * log))                                                  \
    X(void, DeleteProgram, (GLuint program))                                                    \
    X(void, UseProgram, (GLuint program))                                                       \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                          \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose,              \
                               const GLfloat* value))                                           \
    X(void, Uniform3fv, (GLint location, GLsizei count, const GLfloat* value))                  \
    X(void, GenVertexArrays, (GLsizei n, GLuint * arrays))                                      \
    X(void, BindVertexArray, (GLuint array))                                                    \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                              \
    X(void, GenBuffers, (GLsizei n, GLuint * buffers))                                          \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                         \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))       \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                  \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized,   \
                                  GLsizei stride, const void* pointer))                         \
    X(void, EnableVertexAttribArray, (GLuint index))

namespace gl {

#define JS_GL_DECLARE(ret, name, params)        \
    using PFN_##name = ret(APIENTRY*) params; \
    extern PFN_##name name;
JS_GL_FUNCTIONS(JS_GL_DECLARE)
#undef JS_GL_DECLARE

// Loads every function in JS_GL_FUNCTIONS. Requires a current OpenGL context.
// On failure, the names of missing functions are appended to *missing (if non-null).
bool load(std::string* missing);

}  // namespace gl
