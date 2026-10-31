// OpenGL functions used by the shader chain, loaded at run time through SDL (no GL loader library).
// Call them as gl.glBindTexture(...). Needs an OpenGL 3.0+ compatibility context.
#pragma once
#define GL_GLEXT_PROTOTYPES 1   // only so decltype() below can see the prototypes; nothing links to libGL
#include <SDL3/SDL_opengl.h>
#include <string>

#define MP_GL_FUNCS(X)                                                                                  \
    X(glGetString) X(glGetError) X(glGetIntegerv) X(glEnable) X(glDisable) X(glViewport) X(glClear)     \
    X(glClearColor) X(glColorMask) X(glDrawArrays) X(glPixelStorei)                                     \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D)            \
    X(glTexParameteri) X(glTexParameterfv) X(glActiveTexture) X(glGenerateMipmap)                       \
    X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D)         \
    X(glCheckFramebufferStatus)                                                                         \
    X(glGenBuffers) X(glDeleteBuffers) X(glBindBuffer) X(glBufferData)                                  \
    X(glCreateShader) X(glDeleteShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv)           \
    X(glGetShaderInfoLog) X(glCreateProgram) X(glDeleteProgram) X(glAttachShader) X(glLinkProgram)      \
    X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram) X(glGetUniformLocation)                    \
    X(glGetAttribLocation) X(glGetActiveAttrib) X(glBindAttribLocation) X(glUniform1i) X(glUniform1f) X(glUniform2f)             \
    X(glUniformMatrix4fv) X(glVertexAttribPointer) X(glEnableVertexAttribArray)                         \
    X(glDisableVertexAttribArray) X(glVertexAttrib4f) X(glDepthFunc) X(glReadPixels)

struct GlApi {
#define MP_GL_MEMBER(name) decltype(&::name) name = nullptr;
    MP_GL_FUNCS(MP_GL_MEMBER)
#undef MP_GL_MEMBER
};
extern GlApi gl;

// Loads every function for the current context. On failure, err names what is missing.
bool gl_load(std::string& err);
