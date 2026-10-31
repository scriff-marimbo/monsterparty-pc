#include "shader_chain.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include "gl_api.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8   // stbi_load takes UTF-8 paths on Windows too
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WARNINGS
#include "stb_image.h"

// ------------------------------------------------------------------ files and paths

static bool read_file(const std::string& path, std::string& out) {
    std::vector<unsigned char> d;
    if (!mp_read_file(path, d)) return false;
    out.assign(d.begin(), d.end());
    return true;
}

static bool file_exists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

static std::string dir_of(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string("./") : p.substr(0, s + 1);
}

static std::string join(const std::string& dir, const std::string& rel) {
    if (!rel.empty() && mp_is_absolute(rel)) return rel;
    return dir + rel;
}

std::string shader_root() {
    if (const char* base = SDL_GetBasePath()) {
        std::string d = std::string(base) + "shaders/";
        if (file_exists(d + "stock.glsl")) return d;
    }
#ifdef MP_SHADER_DIR
    if (file_exists(std::string(MP_SHADER_DIR) + "/stock.glsl")) return std::string(MP_SHADER_DIR) + "/";
#endif
    return "";
}

std::string resolve_shader_path(const std::string& p) {
    if (p.empty() || mp_is_absolute(p)) return p;
    return shader_root() + p;
}

// ------------------------------------------------------------------ preset (.glslp) parsing

namespace {

struct ConfValue { std::string value, dir; };   // dir: folder of the file the key came from
using Conf = std::map<std::string, ConfValue>;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string unquote(const std::string& raw) {
    std::string v = trim(raw);
    if (!v.empty() && v[0] == '"') {
        size_t e = v.find('"', 1);
        return v.substr(1, e == std::string::npos ? std::string::npos : e - 1);
    }
    size_t h = v.find('#');
    return trim(h == std::string::npos ? v : v.substr(0, h));
}

bool parse_conf(const std::string& path, Conf& conf, std::string& err, int depth = 0) {
    if (depth > 16) { err = "too many nested #reference lines in " + path; return false; }
    std::string text;
    if (!read_file(path, text)) { err = "cannot read " + path; return false; }
    const std::string dir = dir_of(path);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.rfind("#reference", 0) == 0) {
            if (!parse_conf(join(dir, unquote(line.substr(10))), conf, err, depth + 1)) return false;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        conf[trim(line.substr(0, eq))] = {unquote(line.substr(eq + 1)), dir};
    }
    return true;
}

const ConfValue* get(const Conf& c, const std::string& key) {
    auto it = c.find(key);
    return it == c.end() || it->second.value.empty() ? nullptr : &it->second;
}

int get_bool(const Conf& c, const std::string& key) {   // -1 = not set
    const ConfValue* v = get(c, key);
    if (!v) return -1;
    return v->value == "true" || v->value == "1" ? 1 : 0;
}

GLint wrap_mode(const std::string& s) {
    if (s == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
    if (s == "repeat") return GL_REPEAT;
    if (s == "mirrored_repeat") return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_BORDER;   // "clamp_to_border", RetroArch's default
}

enum ScaleType { SCALE_SOURCE, SCALE_VIEWPORT, SCALE_ABSOLUTE };

struct Param { std::string name; float value; };

struct TexBind { GLint tex = -1, tex_size = -1, input_size = -1; int src = -1; };   // src -1 = the game frame

struct Attr { GLint loc; bool array; };

struct Pass {
    // from the preset
    std::string path;
    int filter = -1;                  // -1 unspecified (nearest), 0 nearest, 1 linear: for this pass's input
    GLint wrap = GL_CLAMP_TO_BORDER;  // for this pass's input
    unsigned frame_count_mod = 0;
    bool srgb = false, fp = false, mipmap_input = false;
    std::string alias;
    bool scale_valid = false;
    ScaleType type_x = SCALE_SOURCE, type_y = SCALE_SOURCE;
    float scale_x = 1, scale_y = 1;
    int abs_x = 0, abs_y = 0;

    // OpenGL
    GLuint prog = 0, fbo = 0, tex = 0;
    int out_w = 0, out_h = 0;         // size of tex
    bool to_screen = false;
    GLint u_mvp = -1, u_texture = -1, u_input_size = -1, u_texture_size = -1, u_output_size = -1;
    GLint u_frame_count = -1, u_frame_direction = -1, u_final_vp = -1;
    std::vector<TexBind> binds;
    std::vector<std::pair<GLint, int>> luts;          // uniform, index into Impl::luts
    std::vector<std::pair<GLint, int>> params;        // uniform, index into Impl::params
    std::vector<Attr> attrs;
};

struct Lut { std::string name; GLuint tex = 0; };

// The extra pass used when the preset's last pass draws to a framebuffer of its own size.
const char* kStockShader = R"(#version 110
#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
uniform mat4 MVPMatrix;
varying vec2 uv;
void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }
#elif defined(FRAGMENT)
uniform sampler2D Texture;
varying vec2 uv;
void main() { gl_FragColor = vec4(texture2D(Texture, uv).rgb, 1.0); }
#endif
)";

}  // namespace

struct ShaderChain::Impl {
    std::vector<Pass> passes;
    std::vector<Lut> luts;
    std::vector<Param> params;
    GLuint orig_tex = 0, vbo = 0;
    int orig_w = 0, orig_h = 0;
    uint32_t frame_count = 0;

    ~Impl() {
        for (auto& p : passes) {
            if (p.prog) gl.glDeleteProgram(p.prog);
            if (p.fbo) gl.glDeleteFramebuffers(1, &p.fbo);
            if (p.tex) gl.glDeleteTextures(1, &p.tex);
        }
        for (auto& l : luts) if (l.tex) gl.glDeleteTextures(1, &l.tex);
        if (orig_tex) gl.glDeleteTextures(1, &orig_tex);
        if (vbo) gl.glDeleteBuffers(1, &vbo);
    }
};

// ------------------------------------------------------------------ compiling

// Reads "#pragma parameter NAME "Description" default min max step" lines (blanking them, as
// RetroArch does, since the quotes aren't valid GLSL) and adds new parameters to params.
static void take_parameters(std::string& src, std::vector<Param>& params) {
    size_t pos = 0;
    while ((pos = src.find("#pragma parameter", pos)) != std::string::npos) {
        size_t end = src.find('\n', pos);
        if (end == std::string::npos) end = src.size();
        std::string line = src.substr(pos + 17, end - pos - 17);
        char name[128] = {};
        float def = 0;
        const char* q2 = nullptr;
        if (std::sscanf(line.c_str(), " %127s", name) == 1) {
            const char* q1 = std::strchr(line.c_str(), '"');
            q2 = q1 ? std::strchr(q1 + 1, '"') : nullptr;
            if (q2 && std::sscanf(q2 + 1, " %f", &def) == 1) {
                bool seen = false;
                for (auto& p : params) seen |= p.name == name;
                if (!seen) params.push_back({name, def});
            }
        }
        std::fill(src.begin() + long(pos), src.begin() + long(end), ' ');
        pos = end;
    }
}

static bool compile_stage(GLenum type, const std::string& version, const std::string& defines,
                          const std::string& body, GLuint& out, std::string& err) {
    GLuint sh = gl.glCreateShader(type);
    const char* parts[3] = {version.c_str(), defines.c_str(), body.c_str()};
    gl.glShaderSource(sh, 3, parts, nullptr);
    gl.glCompileShader(sh);
    GLint ok = 0;
    gl.glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        gl.glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        err = std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + log;
        gl.glDeleteShader(sh);
        return false;
    }
    out = sh;
    return true;
}

static bool build_program(const std::string& source_in, const std::string& alias_defines, GLuint& prog, std::string& err) {
    // Like RetroArch: the shader's own #version goes first, and everything up to and including
    // the version number is dropped from the body.
    std::string version, body = source_in;
    size_t v = body.find("#version");
    if (v != std::string::npos) {
        char* after = nullptr;
        unsigned long n = std::strtoul(body.c_str() + v + 8, &after, 10);
        version = "#version " + std::to_string(n) + "\n";
        body = std::string(after);
    }
    const std::string common = "#define PARAMETER_UNIFORM\n" + alias_defines;
    GLuint vs = 0, fs = 0;
    if (!compile_stage(GL_VERTEX_SHADER, version, "#define VERTEX\n" + common, body, vs, err)) return false;
    if (!compile_stage(GL_FRAGMENT_SHADER, version, "#define FRAGMENT\n" + common, body, fs, err)) { gl.glDeleteShader(vs); return false; }
    prog = gl.glCreateProgram();
    gl.glAttachShader(prog, vs);
    gl.glAttachShader(prog, fs);
    gl.glBindAttribLocation(prog, 0, "VertexCoord");   // keep attribute 0 an array (some compat drivers need it)
    gl.glLinkProgram(prog);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);
    GLint ok = 0;
    gl.glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        gl.glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        err = std::string("link: ") + log;
        gl.glDeleteProgram(prog);
        prog = 0;
        return false;
    }
    return true;
}

// Uniform and attribute locations, following RetroArch's gl_glsl_find_uniforms().
static void find_locations(Pass& p, int index, const std::vector<Pass>& passes, const std::vector<Lut>& luts,
                           const std::vector<Param>& params) {
    GLuint prog = p.prog;
    auto U = [&](const std::string& n) { return gl.glGetUniformLocation(prog, n.c_str()); };
    p.u_mvp = U("MVPMatrix");
    p.u_texture = U("Texture");
    p.u_input_size = U("InputSize");
    p.u_texture_size = U("TextureSize");
    p.u_output_size = U("OutputSize");
    p.u_frame_count = U("FrameCount");
    p.u_frame_direction = U("FrameDirection");
    p.u_final_vp = U("FinalViewportSize");

    auto bind = [&](const std::string& base, int src) {
        TexBind b{U(base + "Texture"), U(base + "TextureSize"), U(base + "InputSize"), src};
        if (b.tex >= 0 || b.tex_size >= 0 || b.input_size >= 0) p.binds.push_back(b);
    };
    bind("Orig", -1);
    bind("PassPrev" + std::to_string(index + 1), -1);   // PassPrev(N+1) of pass N is the game frame
    for (int j = 0; j < index; j++) {
        bind("Pass" + std::to_string(j + 1), j);
        bind("PassPrev" + std::to_string(index - j), j);
        if (!passes[j].alias.empty()) bind(passes[j].alias, j);
    }
    bind("Prev", -1);   // frame history isn't kept: previous frames are the current one
    for (int k = 1; k < 7; k++) bind("Prev" + std::to_string(k), -1);

    for (size_t i = 0; i < luts.size(); i++) {
        GLint l = U(luts[i].name);
        if (l >= 0) p.luts.push_back({l, int(i)});
    }
    for (size_t i = 0; i < params.size(); i++) {
        GLint l = U(params[i].name);
        if (l >= 0) p.params.push_back({l, int(i)});
    }

    GLint count = 0;
    gl.glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &count);
    for (GLint i = 0; i < count; i++) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        gl.glGetActiveAttrib(prog, GLuint(i), sizeof name, nullptr, &size, &type, name);
        if (!std::strncmp(name, "gl_", 3)) continue;
        GLint loc = gl.glGetAttribLocation(prog, name);
        if (loc < 0) continue;
        std::string n = name;
        bool array = n == "VertexCoord" || (n.size() >= 8 && n.compare(n.size() - 8, 8, "TexCoord") == 0);
        p.attrs.push_back({loc, array});   // anything else (COLOR) gets a constant white
    }
}

static void set_sampling(GLuint tex, int filter, GLint wrap, bool mipmap) {
    gl.glBindTexture(GL_TEXTURE_2D, tex);
    const bool linear = filter == 1;
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                       mipmap ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (linear ? GL_LINEAR : GL_NEAREST));
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    const GLfloat border[4] = {0, 0, 0, 0};
    gl.glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
}

// ------------------------------------------------------------------ ShaderChain

void ShaderChain::destroy() {
    delete impl;
    impl = nullptr;
}

bool ShaderChain::load(const std::string& preset_path, std::string& err) {
    destroy();
    auto* m = new Impl;
    auto fail = [&](const std::string& e) { err = e; delete m; return false; };

    // ---- read the preset (a bare .glsl file is a one-pass preset)
    Conf conf;
    const bool single = preset_path.size() > 5 && preset_path.compare(preset_path.size() - 5, 5, ".glsl") == 0;
    int npasses = 1;
    if (single) {
        conf["shader0"] = {preset_path, ""};
    } else {
        if (!parse_conf(preset_path, conf, err)) return fail(err);
        const ConfValue* n = get(conf, "shaders");
        npasses = n ? std::atoi(n->value.c_str()) : 0;
        if (npasses < 1 || npasses > 64) return fail("no shaders in " + preset_path);
    }

    for (int i = 0; i < npasses; i++) {
        const std::string n = std::to_string(i);
        Pass p;
        const ConfValue* sh = get(conf, "shader" + n);
        if (!sh) return fail("preset has no shader" + n);
        p.path = join(sh->dir, sh->value);
        p.filter = get_bool(conf, "filter_linear" + n);
        if (const ConfValue* w = get(conf, "wrap_mode" + n)) p.wrap = wrap_mode(w->value);
        if (const ConfValue* f = get(conf, "frame_count_mod" + n)) p.frame_count_mod = unsigned(std::strtoul(f->value.c_str(), nullptr, 0));
        p.srgb = get_bool(conf, "srgb_framebuffer" + n) == 1;
        p.fp = get_bool(conf, "float_framebuffer" + n) == 1;
        p.mipmap_input = get_bool(conf, "mipmap_input" + n) == 1;
        if (const ConfValue* a = get(conf, "alias" + n)) p.alias = a->value;

        auto type_of = [&](const ConfValue* v, ScaleType& t) {
            if (!v) return true;
            if (v->value == "source") t = SCALE_SOURCE;
            else if (v->value == "viewport") t = SCALE_VIEWPORT;
            else if (v->value == "absolute") t = SCALE_ABSOLUTE;
            else return false;
            return true;
        };
        const ConfValue *st = get(conf, "scale_type" + n), *stx = get(conf, "scale_type_x" + n), *sty = get(conf, "scale_type_y" + n);
        if (st) stx = sty = st;
        if (stx || sty) {
            p.scale_valid = true;
            if (!type_of(stx, p.type_x) || !type_of(sty, p.type_y)) return fail("bad scale type in pass " + n);
            const ConfValue* s = get(conf, "scale" + n);
            const ConfValue* sx = s ? s : get(conf, "scale_x" + n);
            const ConfValue* sy = s ? s : get(conf, "scale_y" + n);
            if (p.type_x == SCALE_ABSOLUTE) p.abs_x = sx ? int(std::lround(std::atof(sx->value.c_str()))) : 0;
            else if (sx) p.scale_x = float(std::atof(sx->value.c_str()));
            if (p.type_y == SCALE_ABSOLUTE) p.abs_y = sy ? int(std::lround(std::atof(sy->value.c_str()))) : 0;
            else if (sy) p.scale_y = float(std::atof(sy->value.c_str()));
        }
        m->passes.push_back(p);
    }

    // The last pass draws straight to the window unless it asks for a size of its own (other than
    // the viewport's); then a plain extra pass scales its result to the window, as RetroArch does.
    {
        Pass& last = m->passes.back();
        bool direct = !last.scale_valid || (last.type_x == SCALE_VIEWPORT && last.type_y == SCALE_VIEWPORT &&
                                            last.scale_x == 1.0f && last.scale_y == 1.0f);
        if (!direct) { Pass stock; stock.path = "(stock)"; m->passes.push_back(stock); }
        m->passes.back().to_screen = true;
    }

    // ---- lookup textures
    if (const ConfValue* t = get(conf, "textures")) {
        std::string list = t->value;
        size_t start = 0;
        while (start <= list.size()) {
            size_t semi = list.find(';', start);
            std::string id = trim(list.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
            start = semi == std::string::npos ? list.size() + 1 : semi + 1;
            if (id.empty()) continue;
            const ConfValue* file = get(conf, id);
            if (!file) return fail("no file given for texture " + id);
            std::string path = join(file->dir, file->value);
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
            if (!px) return fail("cannot load texture " + path);
            Lut lut;
            lut.name = id;
            gl.glGenTextures(1, &lut.tex);
            gl.glBindTexture(GL_TEXTURE_2D, lut.tex);
            gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
            const bool mip = get_bool(conf, id + "_mipmap") == 1;
            if (mip) gl.glGenerateMipmap(GL_TEXTURE_2D);
            int lin = get_bool(conf, id + "_linear");
            const ConfValue* wm = get(conf, id + "_wrap_mode");
            set_sampling(lut.tex, lin == 0 ? 0 : 1, wm ? wrap_mode(wm->value) : GL_CLAMP_TO_BORDER, mip);
            m->luts.push_back(lut);
        }
    }

    // ---- compile
    std::string alias_defines;
    for (auto& p : m->passes) if (!p.alias.empty()) alias_defines += "#define " + p.alias + "_ALIAS\n";
    for (size_t i = 0; i < m->passes.size(); i++) {
        Pass& p = m->passes[i];
        std::string src;
        if (p.path == "(stock)") src = kStockShader;
        else if (!read_file(p.path, src)) return fail("cannot read " + p.path);
        take_parameters(src, m->params);
        std::string e;
        if (!build_program(src, alias_defines, p.prog, e)) return fail("pass " + std::to_string(i) + " (" + p.path + "): " + e);
    }
    // a preset can override parameter defaults by name
    for (auto& prm : m->params)
        if (const ConfValue* v = get(conf, prm.name)) prm.value = float(std::atof(v->value.c_str()));
    for (size_t i = 0; i < m->passes.size(); i++) find_locations(m->passes[i], int(i), m->passes, m->luts, m->params);

    // ---- textures and framebuffers. Each texture is sampled the way the pass that reads it asks.
    gl.glGenTextures(1, &m->orig_tex);
    set_sampling(m->orig_tex, m->passes[0].filter, m->passes[0].wrap, m->passes[0].mipmap_input);
    for (size_t i = 0; i + 1 < m->passes.size(); i++) {
        Pass& p = m->passes[i];
        const Pass& next = m->passes[i + 1];
        gl.glGenFramebuffers(1, &p.fbo);
        gl.glGenTextures(1, &p.tex);
        set_sampling(p.tex, next.filter, next.wrap, next.mipmap_input);
    }
    const GLfloat quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    gl.glGenBuffers(1, &m->vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (GLenum e = gl.glGetError(); e != GL_NO_ERROR) {
        char b[64];
        std::snprintf(b, sizeof b, "OpenGL error 0x%04X while setting up", e);
        return fail(b);
    }
    impl = m;
    return true;
}

static int scaled(ScaleType t, float s, int abs, int source, int viewport) {
    int v = t == SCALE_ABSOLUTE ? abs : int(std::lround((t == SCALE_VIEWPORT ? viewport : source) * double(s)));
    return std::max(v, 1);
}

void ShaderChain::render(const uint32_t* argb, int src_w, int src_h, int x, int y, int w, int h, int fb_w, int fb_h) {
    if (!impl) return;
    Impl& m = *impl;
    m.frame_count++;

    // ---- game frame
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, m.orig_tex);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (m.orig_w != src_w || m.orig_h != src_h) {
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
        m.orig_w = src_w;
        m.orig_h = src_h;
    } else {
        gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    }
    if (m.passes[0].mipmap_input) gl.glGenerateMipmap(GL_TEXTURE_2D);

    // ---- pass sizes; (re)allocate framebuffers that changed
    int sw = src_w, sh = src_h;
    for (auto& p : m.passes) {
        int ow, oh;
        if (p.to_screen) { ow = w; oh = h; }
        else if (!p.scale_valid) { ow = sw; oh = sh; }
        else { ow = scaled(p.type_x, p.scale_x, p.abs_x, sw, w); oh = scaled(p.type_y, p.scale_y, p.abs_y, sh, h); }
        if (!p.to_screen && (ow != p.out_w || oh != p.out_h)) {
            gl.glBindTexture(GL_TEXTURE_2D, p.tex);
            if (p.fp) gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ow, oh, 0, GL_RGBA, GL_FLOAT, nullptr);
            else gl.glTexImage2D(GL_TEXTURE_2D, 0, p.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.tex, 0);
        }
        p.out_w = ow;
        p.out_h = oh;
        sw = ow;
        sh = oh;
    }

    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glDisable(GL_FRAMEBUFFER_SRGB);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.glViewport(0, 0, fb_w, fb_h);
    gl.glClearColor(0, 0, 0, 1);
    gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m.vbo);

    static const GLfloat mvp_fbo[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
    static const GLfloat mvp_screen[16] = {2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1};   // textures are top row first

    for (size_t i = 0; i < m.passes.size(); i++) {
        Pass& p = m.passes[i];
        const GLuint in_tex = i == 0 ? m.orig_tex : m.passes[i - 1].tex;
        const int in_w = i == 0 ? src_w : m.passes[i - 1].out_w, in_h = i == 0 ? src_h : m.passes[i - 1].out_h;

        if (p.to_screen) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.glDisable(GL_FRAMEBUFFER_SRGB);
            gl.glViewport(x, fb_h - y - h, w, h);
        } else {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
            if (p.srgb) gl.glEnable(GL_FRAMEBUFFER_SRGB); else gl.glDisable(GL_FRAMEBUFFER_SRGB);
            gl.glViewport(0, 0, p.out_w, p.out_h);
        }
        gl.glUseProgram(p.prog);
        if (p.u_mvp >= 0) gl.glUniformMatrix4fv(p.u_mvp, 1, GL_FALSE, p.to_screen ? mvp_screen : mvp_fbo);
        if (p.u_input_size >= 0) gl.glUniform2f(p.u_input_size, float(in_w), float(in_h));
        if (p.u_texture_size >= 0) gl.glUniform2f(p.u_texture_size, float(in_w), float(in_h));
        if (p.u_output_size >= 0) gl.glUniform2f(p.u_output_size, float(p.out_w), float(p.out_h));
        if (p.u_final_vp >= 0) gl.glUniform2f(p.u_final_vp, float(w), float(h));
        if (p.u_frame_count >= 0) gl.glUniform1i(p.u_frame_count, int(p.frame_count_mod ? m.frame_count % p.frame_count_mod : m.frame_count));
        if (p.u_frame_direction >= 0) gl.glUniform1i(p.u_frame_direction, 1);
        for (auto& [loc, idx] : p.params) gl.glUniform1f(loc, m.params[size_t(idx)].value);

        int unit = 0;
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, in_tex);
        if (p.u_texture >= 0) gl.glUniform1i(p.u_texture, 0);
        for (auto& b : p.binds) {
            const GLuint t = b.src < 0 ? m.orig_tex : m.passes[size_t(b.src)].tex;
            const float bw = float(b.src < 0 ? src_w : m.passes[size_t(b.src)].out_w);
            const float bh = float(b.src < 0 ? src_h : m.passes[size_t(b.src)].out_h);
            if (b.tex >= 0) {
                ++unit;
                gl.glActiveTexture(GL_TEXTURE0 + GLenum(unit));
                gl.glBindTexture(GL_TEXTURE_2D, t);
                gl.glUniform1i(b.tex, unit);
            }
            if (b.tex_size >= 0) gl.glUniform2f(b.tex_size, bw, bh);
            if (b.input_size >= 0) gl.glUniform2f(b.input_size, bw, bh);
        }
        for (auto& [loc, idx] : p.luts) {
            ++unit;
            gl.glActiveTexture(GL_TEXTURE0 + GLenum(unit));
            gl.glBindTexture(GL_TEXTURE_2D, m.luts[size_t(idx)].tex);
            gl.glUniform1i(loc, unit);
        }

        for (auto& a : p.attrs) {
            if (a.array) {
                gl.glEnableVertexAttribArray(GLuint(a.loc));
                gl.glVertexAttribPointer(GLuint(a.loc), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            } else {
                gl.glDisableVertexAttribArray(GLuint(a.loc));
                gl.glVertexAttrib4f(GLuint(a.loc), 1, 1, 1, 1);
            }
        }
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        for (auto& a : p.attrs) if (a.array) gl.glDisableVertexAttribArray(GLuint(a.loc));

        if (!p.to_screen && i + 1 < m.passes.size() && m.passes[i + 1].mipmap_input) {
            gl.glActiveTexture(GL_TEXTURE0);
            gl.glBindTexture(GL_TEXTURE_2D, p.tex);
            gl.glGenerateMipmap(GL_TEXTURE_2D);
        }
        gl.glActiveTexture(GL_TEXTURE0);
    }
    gl.glUseProgram(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl.glDisable(GL_FRAMEBUFFER_SRGB);
}
