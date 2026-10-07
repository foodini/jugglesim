// gl_funcs.cpp - see gl_funcs.h.
#include "gl_funcs.h"

#include "platform.h"

#include <cstdint>

namespace gl {

#define JS_GL_DEFINE(ret, name, params) PFN_##name name = nullptr;
JS_GL_FUNCTIONS(JS_GL_DEFINE)
#undef JS_GL_DEFINE

namespace {

#if defined(_WIN32)
// wglGetProcAddress only returns extension / post-1.1 functions, and some drivers signal failure
// with small sentinel values instead of nullptr. Fall back to opengl32.dll for anything it exports.
PROC getProc(const char* name) {
    PROC p = ::wglGetProcAddress(name);
    const std::intptr_t v = reinterpret_cast<std::intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
        static HMODULE opengl32 = ::LoadLibraryA("opengl32.dll");
        p = opengl32 ? ::GetProcAddress(opengl32, name) : nullptr;
    }
    return p;
}
#else
void* getProc(const char* name) { return platformGetProcAddress(name); }
#endif

}  // namespace

bool load(std::string* missing) {
    bool ok = true;
#define JS_GL_LOAD(ret, name, params)                               \
    name = reinterpret_cast<PFN_##name>(getProc("gl" #name));       \
    if (!name) {                                                    \
        ok = false;                                                 \
        if (missing) *missing += "gl" #name "\n";                   \
    }
    JS_GL_FUNCTIONS(JS_GL_LOAD)
#undef JS_GL_LOAD
    return ok;
}

}  // namespace gl
