// Monster Party native port: 3D mode (drawing only; the game runs exactly as in 2D).
//
// Nes::render_frame fills a Scene3D (nes.h) each frame; this draws it with OpenGL:
//   - solid cells (the game's collision types 4 and 2: floors, walls, platforms) as blocks 16 px
//     deep, their front face exactly the 2D picture, their top, bottom and sides the cell's whole
//     16 x 16 metatile across the depth;
//   - the rest of the background (doors, exits, scenery) flat, on a wall at the blocks' back face;
//   - the sprites on one flat layer just in front of the blocks;
//   - the status bar flat over the bottom of the window, not in 3D.
// Screens that aren't a room (title, story, password...) are one flat picture. The mouse turns the
// camera around the middle of the screen, the wheel zooms, a right click puts it back.
#pragma once
#include <string>
#include "nes.h"

struct Camera3D {
    float yaw = 0, pitch = 0;          // degrees
    float zoom = 1;                    // 1 = the playfield fills the window's height, as in 2D
    void reset() { yaw = pitch = 0; zoom = 1; }
};

class Renderer3D {
public:
    // Needs a current OpenGL 3.0+ context (and gl_load done). err says why not.
    bool init(std::string& err);
    // Draws the scene into the current framebuffer (window size in pixels). pixel_aspect: 8/7 for
    // NES TV pixels, 1 for square ones.
    void draw(const Scene3D& sc, const uint32_t* frame, int win_w, int win_h, float pixel_aspect, const Camera3D& cam);
private:
    unsigned prog_ = 0, vbo_ = 0, tex_bg_ = 0, tex_spr_ = 0, tex_frame_ = 0;
    int loc_mvp_ = -1, loc_tex_ = -1, loc_size_ = -1, loc_tile_ = -1, loc_alpha_ = -1;
    int a_pos_ = -1, a_local_ = -1, a_org_ = -1, a_shade_ = -1;
};
