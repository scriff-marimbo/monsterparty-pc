// 3D mode's scene (Nes::build_scene3d): drawing only, and what it hands to the 3D renderer.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "ram_symbols.h"
#include "settings.h"
#include "support.h"

using namespace mptest;

TEST_CASE("3D mode is drawing only", "[game][3d]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.mode3d = true;
        CHECK(run(g, play_round1(), o).ram_hash == run(g, play_round1()).ram_hash);
    }
}

TEST_CASE("the 3D scene of a room", "[game][3d]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.mode3d = true;
        Result r = run(g, play_round1(), o);
        const Scene3D& sc = *r.scene;
        const Nes& nes = *r.nes;
        REQUIRE(sc.in_room);
        CHECK(sc.w == 256 + 2 * 16);             // not widescreen: a 16 px margin, not shown
        CHECK_FALSE(sc.wide);
        CHECK(sc.bar_x0 == 16);
        CHECK(sc.split > 150);
        CHECK(sc.split <= 240);
        // the cells cover the frame
        CHECK(sc.cell_x0 <= 0);
        CHECK(sc.cell_x0 > -8);
        CHECK(sc.cell_x0 + sc.cells * 8 >= sc.w);
        // round 1 has floor: solid cells (type 4) in the lower rows, and nothing solid at the top
        int solid_low = 0, solid_top = 0;
        for (int c = 0; c < sc.cells; c++) {
            for (int ty = 18; ty < 24; ty++) solid_low += sc.type[ty][c] == 4;
            solid_top += sc.type[0][c] == 4;
        }
        CHECK(solid_low > 10);
        CHECK(solid_top < sc.cells);
        // background + sprites = the 2D frame
        int sprite_px = 0, mismatched = 0;
        for (int i = 0; i < sc.w * 240; i++) {
            const uint32_t want = nes.frame[i];
            const uint32_t got = sc.spr[i] ? sc.spr[i] : sc.bg[i];
            sprite_px += sc.spr[i] != 0;
            mismatched += (got | 0xFF000000u) != (want | 0xFF000000u);
        }
        CHECK(sprite_px > 0);
        CHECK(mismatched == 0);
        // cells come in 16 x 16 metatiles: tx0 says which column starts one, and both halves agree
        int pairs = 0;
        for (int c = 0; c + 1 < sc.cells; c++) {
            if ((sc.tx0 + c) & 1) continue;
            for (int ty = 0; ty < 24; ty += 2) {
                const uint8_t t = sc.type[ty][c];
                CHECK(sc.type[ty][c + 1] == t);
                CHECK(sc.type[ty + 1][c] == t);
                CHECK(sc.type[ty + 1][c + 1] == t);
                pairs++;
            }
        }
        CHECK(pairs > 100);
    }
}

TEST_CASE("a screen that isn't a room is a flat picture in 3D", "[game][3d]") {
    REQUIRE_ROM(Game::US);
    Options o;
    o.mode3d = true;
    Script title;
    title.frames = 150;
    Result r = run(Game::US, title, o);
    CHECK(r.ram[ScreenId] == 1);
    CHECK_FALSE(r.scene->in_room);
    CHECK(r.scene->cells == 0);
}

TEST_CASE("widescreen dims the extra playfield either side, drawing only", "[game][3d][options]") {
    REQUIRE_ROM(Game::US);
    Options plain, dim;
    plain.wide_margin = dim.wide_margin = wide_margin(false);
    plain.mode3d = dim.mode3d = true;                       // (for the scene's play_x0 and split)
    dim.wide_dim = true;
    Result a = run(Game::US, play_round1(), plain), b = run(Game::US, play_round1(), dim);
    CHECK(a.ram_hash == b.ram_hash);
    const Scene3D& sc = *b.scene;
    REQUIRE(sc.in_room);
    CHECK(sc.wide);
    int dimmed = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < sc.w; x++) {
            const uint32_t pa = a.nes->frame[y * sc.w + x], pb = b.nes->frame[y * sc.w + x];
            const bool extra = y < sc.split && (x < sc.play_x0 || x >= sc.play_x0 + 256);
            if (!extra) { CHECK(pa == pb); continue; }
            for (int sh : {0, 8, 16}) CHECK(((pb >> sh) & 0xFF) <= ((pa >> sh) & 0xFF));
            dimmed += pa != pb;
        }
    CHECK(dimmed > 1000);
}

TEST_CASE("widescreen doesn't dim boss rooms", "[game][3d][options]") {
    REQUIRE_ROM(Game::US);
    Options plain, dim;
    plain.wide_margin = dim.wide_margin = wide_margin(false);
    dim.wide_dim = true;
    plain.boss_door = dim.boss_door = 0x01;
    Result a = run(Game::US, enter_first_boss_door(), plain), b = run(Game::US, enter_first_boss_door(), dim);
    REQUIRE((b.ram[BossRoomFlag] & 0x80));
    CHECK(std::equal(a.nes->frame, a.nes->frame + a.nes->frame_w * 240, b.nes->frame));
}
