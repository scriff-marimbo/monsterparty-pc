// Monster Party native port: RetroArch-style GLSL shaders (.glslp presets from libretro/glsl-shaders).
//
// Runs a preset's passes with OpenGL framebuffers, binding the uniforms and textures RetroArch's GLSL
// driver provides: MVPMatrix, Texture, InputSize, TextureSize, OutputSize, FrameCount (with
// frame_count_mod), FrameDirection, FinalViewportSize, Orig*, PassN*, PassPrevN*, pass aliases,
// lookup textures (PNG) and #pragma parameter values. Supported preset keys: shaderN, scale_type*,
// scale*, filter_linearN, wrap_modeN, float_framebufferN, srgb_framebufferN, mipmap_inputN, aliasN,
// frame_count_modN, textures (with _linear, _mipmap, _wrap_mode), parameter overrides and #reference.
// Not supported: frame history (PrevN, which gets the current frame) and feedback passes.
#pragma once
#include <cstdint>
#include <string>

class ShaderChain {
public:
    ShaderChain() = default;
    ShaderChain(const ShaderChain&) = delete;
    ShaderChain& operator=(const ShaderChain&) = delete;
    ~ShaderChain() { destroy(); }

    // Needs a current OpenGL 3.0+ context with gl_load() done. On failure err says why
    // (compiler logs included) and nothing is left allocated.
    bool load(const std::string& preset_path, std::string& err);

    // Draws a src_w x src_h ARGB8888 frame into the rectangle (x, y, w, h) of a fb_w x fb_h
    // drawable (pixels, y measured from the top). The rest of the drawable is cleared to black.
    void render(const uint32_t* argb, int src_w, int src_h, int x, int y, int w, int h, int fb_w, int fb_h);

    void destroy();
    bool loaded() const { return impl != nullptr; }

private:
    struct Impl;
    Impl* impl = nullptr;
};

// Folder holding the libretro glsl-shaders checkout (ends in '/'): "shaders/" next to the
// executable if there is one, otherwise the copy CMake downloaded. Empty if neither exists.
std::string shader_root();

// Settings store bundled presets relative to shader_root(); anything else is an absolute path.
std::string resolve_shader_path(const std::string& p);
