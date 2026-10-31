#include <cstdlib>
#include <string>
#include "gl_api.h"
#include <SDL3/SDL.h>

GlApi gl;

bool gl_load(std::string& err) {
    bool ok = true;
#define MP_GL_LOAD(name)                                                                  \
    gl.name = reinterpret_cast<decltype(gl.name)>(SDL_GL_GetProcAddress(#name));          \
    if (!gl.name) { ok = false; err += std::string(err.empty() ? "" : ", ") + #name; }
    MP_GL_FUNCS(MP_GL_LOAD)
#undef MP_GL_LOAD
    if (!ok) { err = "OpenGL functions missing: " + err; return false; }
    const char* ver = reinterpret_cast<const char*>(gl.glGetString(GL_VERSION));
    int major = ver ? std::atoi(ver) : 0;
    if (major < 3) { err = std::string("shaders need OpenGL 3.0 or newer; this context is ") + (ver ? ver : "unknown"); return false; }
    return true;
}
