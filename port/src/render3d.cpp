// Monster Party native port: 3D mode. See render3d.h.
#include "render3d.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "gl_api.h"

namespace {

// Geometry, in NES pixels
constexpr float BLOCK_DEPTH = 16;      // how far the blocks stick back into the screen
constexpr float FOV_DEG = 40;          // vertical field of view
constexpr float SPRITE_Z = 1;          // the sprite layer, just in front of the blocks

const char* VS = R"(#version 130
uniform mat4 mvp;
in vec3 pos; in vec2 local; in vec2 org; in float shade;
out vec2 v_local; out vec2 v_org; out float v_shade;
void main() { v_local = local; v_org = org; v_shade = shade; gl_Position = mvp * vec4(pos, 1.0); }
)";
// local: texel position within the face (in tile mode, wrapped to 8 so a tile repeats); org: where
// the face's texels start in the texture
const char* FS = R"(#version 130
uniform sampler2D tex; uniform vec2 size; uniform float tile; uniform float alpha_test;
in vec2 v_local; in vec2 v_org; in float v_shade;
out vec4 color;
void main() {
    vec2 l = tile > 0.5 ? mod(v_local, 8.0) : v_local;
    vec2 p = clamp(floor(v_org + l), vec2(0.0), size - 1.0);
    vec4 c = texelFetch(tex, ivec2(p), 0);
    if (alpha_test > 0.5 && c.a < 0.5) discard;
    color = vec4(c.rgb * v_shade, 1.0);
}
)";

struct Mat { float m[16]; };           // column-major, as GL wants
Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
Mat perspective(float fovy_deg, float aspect, float n, float f) {
    const float t = 1.0f / std::tan(fovy_deg * 3.14159265f / 360.0f);
    Mat r{};
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
Mat ortho(float l, float r, float b, float t) {
    Mat m{};
    m.m[0] = 2 / (r - l); m.m[5] = 2 / (t - b); m.m[10] = -1;
    m.m[12] = -(r + l) / (r - l); m.m[13] = -(t + b) / (t - b); m.m[15] = 1;
    return m;
}
Mat look_at(const float eye[3], const float at[3]) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    const float up[3] = {0, 1, 0};
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& v : s) v /= sl;
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat m{};
    m.m[0] = s[0]; m.m[4] = s[1]; m.m[8] = s[2];
    m.m[1] = u[0]; m.m[5] = u[1]; m.m[9] = u[2];
    m.m[2] = -f[0]; m.m[6] = -f[1]; m.m[10] = -f[2];
    m.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m.m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    m.m[15] = 1;
    return m;
}

struct Vtx { float x, y, z, lu, lv, ou, ov, shade; };

// A quad from four corners (in order around it), with the texel coordinates (local) at each.
void quad(std::vector<Vtx>& out, const float p[4][3], const float l[4][2], float ou, float ov, float shade) {
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) out.push_back({p[i][0], p[i][1], p[i][2], l[i][0], l[i][1], ou, ov, shade});
}

unsigned compile(GLenum type, const char* src, std::string& err) {
    unsigned s = gl.glCreateShader(type);
    gl.glShaderSource(s, 1, &src, nullptr);
    gl.glCompileShader(s);
    GLint ok = 0;
    gl.glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl.glGetShaderInfoLog(s, sizeof log, nullptr, log);
        err = std::string("3D shader: ") + log;
        gl.glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned make_texture() {
    unsigned t = 0;
    gl.glGenTextures(1, &t);
    gl.glBindTexture(GL_TEXTURE_2D, t);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void upload(unsigned tex, const uint32_t* px, int w, int h, int stride) {
    gl.glBindTexture(GL_TEXTURE_2D, tex);
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);   // ARGB words = B,G,R,A bytes
    gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

bool solid(uint8_t type) { return type == 4 || type == 2; }

}  // namespace

bool Renderer3D::init(std::string& err) {
    unsigned vs = compile(GL_VERTEX_SHADER, VS, err);
    if (!vs) return false;
    unsigned fs = compile(GL_FRAGMENT_SHADER, FS, err);
    if (!fs) { gl.glDeleteShader(vs); return false; }
    prog_ = gl.glCreateProgram();
    gl.glAttachShader(prog_, vs);
    gl.glAttachShader(prog_, fs);
    gl.glLinkProgram(prog_);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);
    GLint ok = 0;
    gl.glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl.glGetProgramInfoLog(prog_, sizeof log, nullptr, log);
        err = std::string("3D shader: ") + log;
        gl.glDeleteProgram(prog_);
        prog_ = 0;
        return false;
    }
    loc_mvp_ = gl.glGetUniformLocation(prog_, "mvp");
    loc_tex_ = gl.glGetUniformLocation(prog_, "tex");
    loc_size_ = gl.glGetUniformLocation(prog_, "size");
    loc_tile_ = gl.glGetUniformLocation(prog_, "tile");
    loc_alpha_ = gl.glGetUniformLocation(prog_, "alpha_test");
    a_pos_ = gl.glGetAttribLocation(prog_, "pos");
    a_local_ = gl.glGetAttribLocation(prog_, "local");
    a_org_ = gl.glGetAttribLocation(prog_, "org");
    a_shade_ = gl.glGetAttribLocation(prog_, "shade");
    gl.glGenBuffers(1, &vbo_);
    tex_bg_ = make_texture();
    tex_spr_ = make_texture();
    tex_frame_ = make_texture();
    return true;
}

void Renderer3D::draw(const Scene3D& sc, const uint32_t* frame, int win_w, int win_h, float pa, const Camera3D& cam) {
    const int W = sc.w;
    // What to show: the whole (widescreen) frame, or the game's own 256 columns
    const int px0 = sc.wide ? 0 : sc.play_x0, pw = sc.wide ? W : 256;   // playfield
    const int bx0 = sc.wide ? 0 : sc.bar_x0, bw = sc.wide ? W : 256;    // status bar, other screens
    // The window: the 3D view on top, the status bar (rooms only) flat along the bottom, at the
    // same scale as the playfield seen straight on.
    const int rows3d = sc.in_room ? std::clamp(sc.split, 1, 240) : 240;
    const float scale = win_h / 240.0f;
    const int view_h = sc.in_room ? int(std::lround(rows3d * scale)) : win_h;

    gl.glDisable(GL_SCISSOR_TEST);
    gl.glViewport(0, 0, win_w, win_h);
    const uint32_t bd = sc.backdrop;
    gl.glClearColor(((bd >> 16) & 0xFF) / 255.0f, ((bd >> 8) & 0xFF) / 255.0f, (bd & 0xFF) / 255.0f, 1);
    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.glUseProgram(prog_);
    gl.glUniform1i(loc_tex_, 0);
    gl.glActiveTexture(GL_TEXTURE0);

    // world: x = (screen x - W/2) * pixel aspect, y = up, z = 0 at the screen, negative into it
    const float cy_s = rows3d / 2.0f;                      // the screen row the camera looks at
    const float cx_s = sc.in_room ? px0 + pw / 2.0f : bx0 + bw / 2.0f;   // the screen column the camera looks at
    auto X = [&](float sx) { return (sx - cx_s) * pa; };
    auto Y = [&](float sy) { return cy_s - sy; };
    const float dist0 = (rows3d / 2.0f) / std::tan(FOV_DEG * 3.14159265f / 360.0f);   // straight on, rows3d fill the view
    const float dist = dist0 * cam.zoom;
    const float yaw = cam.yaw * 3.14159265f / 180, pitch = cam.pitch * 3.14159265f / 180;
    const float target[3] = {0, 0, 0};                    // turns around the middle of the screen
    const float eye[3] = {target[0] + dist * std::sin(yaw) * std::cos(pitch), target[1] + dist * std::sin(pitch),
                          target[2] + dist * std::cos(yaw) * std::cos(pitch)};
    const Mat mvp = mul(perspective(FOV_DEG, float(win_w) / view_h, dist0 * 0.02f, dist0 * 40), look_at(eye, target));

    std::vector<Vtx> v;
    auto flush = [&](unsigned tex, int tw, int th, bool tile, bool alpha) {
        if (v.empty()) return;
        gl.glBindTexture(GL_TEXTURE_2D, tex);
        gl.glUniform2f(loc_size_, float(tw), float(th));
        gl.glUniform1f(loc_tile_, tile ? 1.0f : 0.0f);
        gl.glUniform1f(loc_alpha_, alpha ? 1.0f : 0.0f);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl.glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(Vtx)), v.data(), GL_STREAM_DRAW);
        const int stride = sizeof(Vtx);
        auto attr = [&](int loc, int n, size_t off) {
            if (loc < 0) return;
            gl.glEnableVertexAttribArray(loc);
            gl.glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off));
        };
        attr(a_pos_, 3, offsetof(Vtx, x));
        attr(a_local_, 2, offsetof(Vtx, lu));
        attr(a_org_, 2, offsetof(Vtx, ou));
        attr(a_shade_, 1, offsetof(Vtx, shade));
        gl.glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size()));
        v.clear();
    };

    gl.glViewport(0, win_h - view_h, win_w, view_h);
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glUniformMatrix4fv(loc_mvp_, 1, GL_FALSE, mvp.m);

    if (!sc.in_room) {
        // a flat picture
        upload(tex_frame_, frame, W, 240, W);
        const float a = float(bx0), b = float(bx0 + bw);
        const float p[4][3] = {{X(a), Y(0), 0}, {X(a), Y(240), 0}, {X(b), Y(240), 0}, {X(b), Y(0), 0}};
        const float l[4][2] = {{a, 0}, {a, 240}, {b, 240}, {b, 0}};
        quad(v, p, l, 0, 0, 1);
        flush(tex_frame_, W, 240, false, false);
        gl.glDisable(GL_DEPTH_TEST);
        return;
    }

    // ---- the back wall: the whole background, flat at the blocks' back face (doors, exits and the
    // rest of the scenery stay where they are in 2D; the blocks' own picture there is hidden by them)
    upload(tex_bg_, sc.bg, W, 240, W);
    const float zf = 0, zk = -BLOCK_DEPTH;
    {
        const float a = float(px0), b = float(px0 + pw);
        const float p[4][3] = {{X(a), Y(0), zk}, {X(a), Y(rows3d), zk}, {X(b), Y(rows3d), zk}, {X(b), Y(0), zk}};
        const float l[4][2] = {{a, 0}, {a, float(rows3d)}, {b, float(rows3d)}, {b, 0}};
        quad(v, p, l, 0, 0, 1);
    }

    // ---- the blocks, textured from the frame's own background. The front is the 2D picture; the
    // top, bottom and sides show the cell's whole 16 x 16 metatile across the block's depth, folded
    // over the front edge (the top's front edge meets the metatile's top row, and so on).
    const int rows = std::min(24, (rows3d + 7) / 8);
    auto type_at = [&](int c, int ty) -> uint8_t {
        if (c < 0 || c >= sc.cells || ty < 0 || ty >= 24) return 0;
        return sc.type[ty][c];
    };
    const float D = BLOCK_DEPTH;
    for (int ty = 0; ty < rows; ty++)
        for (int c = 0; c < sc.cells; c++) {
            if (!solid(sc.type[ty][c])) continue;
            const float sx = float(sc.cell_x0 + c * 8), sy = float(ty * 8);
            if (sx < px0 || sx + 8 > px0 + pw) continue;   // only cells whose picture is all in view
            const float x0 = X(sx), x1 = X(sx + 8), y0 = Y(sy), y1 = Y(sy + 8);
            // the metatile this cell is a quarter of: its top-left in the frame, and where the cell sits in it
            const float mx = sx - float(((sc.tx0 + c) & 1) * 8), my = float((ty & ~1) * 8);
            const float cu = sx - mx, cv = sy - my;        // 0 or 8
            {   // front: the 2D picture
                const float p[4][3] = {{x0, y0, zf}, {x0, y1, zf}, {x1, y1, zf}, {x1, y0, zf}};
                const float l[4][2] = {{0, 0}, {0, 8}, {8, 8}, {8, 0}};
                quad(v, p, l, sx, sy, 1);
            }
            if (!solid(type_at(c, ty - 1))) {   // top: metatile rows 0-15 from the front edge back
                const float p[4][3] = {{x0, y0, zk}, {x0, y0, zf}, {x1, y0, zf}, {x1, y0, zk}};
                const float l[4][2] = {{cu, D}, {cu, 0}, {cu + 8, 0}, {cu + 8, D}};
                quad(v, p, l, mx, my, 0.85f);
            }
            if (!solid(type_at(c, ty + 1))) {   // bottom: rows 15-0 from the front edge back
                const float p[4][3] = {{x0, y1, zf}, {x0, y1, zk}, {x1, y1, zk}, {x1, y1, zf}};
                const float l[4][2] = {{cu, D}, {cu, 0}, {cu + 8, 0}, {cu + 8, D}};
                quad(v, p, l, mx, my, 0.55f);
            }
            if (!solid(type_at(c - 1, ty))) {   // left: columns 0-15 from the front edge back
                const float p[4][3] = {{x0, y0, zk}, {x0, y1, zk}, {x0, y1, zf}, {x0, y0, zf}};
                const float l[4][2] = {{D, cv}, {D, cv + 8}, {0, cv + 8}, {0, cv}};
                quad(v, p, l, mx, my, 0.7f);
            }
            if (!solid(type_at(c + 1, ty))) {   // right: columns 15-0 from the front edge back
                const float p[4][3] = {{x1, y0, zf}, {x1, y1, zf}, {x1, y1, zk}, {x1, y0, zk}};
                const float l[4][2] = {{D, cv}, {D, cv + 8}, {0, cv + 8}, {0, cv}};
                quad(v, p, l, mx, my, 0.7f);
            }
        }
    flush(tex_bg_, W, 240, false, false);

    // ---- the sprites, flat, just in front of the blocks
    {
        upload(tex_spr_, sc.spr, W, rows3d, W);
        const float a = float(px0), b = float(px0 + pw);
        const float p[4][3] = {{X(a), Y(0), SPRITE_Z}, {X(a), Y(rows3d), SPRITE_Z}, {X(b), Y(rows3d), SPRITE_Z}, {X(b), Y(0), SPRITE_Z}};
        const float l[4][2] = {{a, 0}, {a, float(rows3d)}, {b, float(rows3d)}, {b, 0}};
        quad(v, p, l, 0, 0, 1);
        flush(tex_spr_, W, rows3d, false, true);
    }

    // ---- the status bar, flat along the bottom
    gl.glDisable(GL_DEPTH_TEST);
    if (rows3d < 240) {
        const int bar_h = win_h - view_h;
        gl.glViewport(0, 0, win_w, bar_h);
        // same pixel size as the playfield straight on, centred
        const float half = win_w / (2.0f * scale * pa);    // NES pixels from the middle to the window's edge
        const float mid = bx0 + bw / 2.0f;
        const Mat m = ortho(mid - half, mid + half, 240, float(rows3d));
        gl.glUniformMatrix4fv(loc_mvp_, 1, GL_FALSE, m.m);
        upload(tex_frame_, frame, W, 240, W);
        const float a = float(bx0), b = float(bx0 + bw);
        const float p[4][3] = {{a, float(rows3d), 0}, {a, 240, 0}, {b, 240, 0}, {b, float(rows3d), 0}};
        const float l[4][2] = {{a, float(rows3d)}, {a, 240}, {b, 240}, {b, float(rows3d)}};
        quad(v, p, l, 0, 0, 1);
        flush(tex_frame_, W, 240, false, false);
    }
    gl.glViewport(0, 0, win_w, win_h);
}
