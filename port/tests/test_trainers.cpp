// Trainers (cheats) and the is_cheating flag.
#include <catch2/catch_test_macros.hpp>
#include "ram_symbols.h"
#include "support.h"
#include "custom_sprites.h"

using namespace mptest;

TEST_CASE("Start with Full Life", "[game][trainers]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.trainers.full_life = true;
        Result r = run(g, start_game(), o);
        CHECK(r.ram[PlayerHealth] == 0x2C);
        CHECK(r.is_cheating);
    }
}

TEST_CASE("Start on Round N", "[game][trainers]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        for (int round : {2, 5, 8}) {
            Options o;
            o.trainers.start_round = round;
            Result r = run(g, start_game(), o);
            INFO("round " << round);
            CHECK(r.ram[RoundNumber] == round - 1);
            CHECK(r.ram[GameState] == 5);
            CHECK(r.is_cheating);
        }
    }
}

TEST_CASE("Always play as Bert", "[game][trainers]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.trainers.always_bert = true;
        Result r = run(g, play_round1(), o);
        CHECK((r.ram[PlayerForm] & 0x80));
        CHECK(r.ram[BertTimer] == 1);          // held, so it never reaches $23
    }
}

TEST_CASE("Infinite Life keeps the health through hits", "[game][trainers]") {
    REQUIRE_ROM(Game::US);
    // a long walk right through round 1 gets hit; with the trainer the health stays
    Script s = play_round1(4000);
    for (uint64_t f = 2600; f < 4000; f += 8) s.presses.push_back({f, BTN_RIGHT});
    Result plain = run(Game::US, s);
    Options o;
    o.trainers.infinite_life = true;
    Result r = run(Game::US, s, o);
    CHECK(plain.ram[PlayerHealth] < 0x0C);     // the walk does get hit...
    CHECK(r.ram[PlayerHealth] >= 0x0C);        // ...but with the trainer no health is lost
}

TEST_CASE("Super and Moon Jump go higher", "[game][trainers]") {
    REQUIRE_ROM(Game::US);
    // jump once in round 1 and note the highest point (lowest ObjY) the player reaches
    auto peak = [](const Trainers& t) {
        Options o;
        o.trainers = t;
        int best = 255;
        Script s = start_game(1500);
        s.presses.push_back({1250, BTN_A});
        s.watch = [&](const Nes& n) { if (n.frame_count >= 1250) best = std::min(best, int(n.cpu.ram[0x3CB])); };
        run(Game::US, s, o);
        return best;
    };
    const int normal = peak({});
    Trainers super; super.super_jump = true;
    Trainers moon; moon.moon_jump = true;
    const int sup = peak(super), mn = peak(moon);
    INFO("highest ObjY: normal " << normal << ", super " << sup << ", moon " << mn);
    CHECK(sup < normal);
    CHECK(mn <= sup);
}

TEST_CASE("is_cheating", "[game][trainers]") {
    REQUIRE_ROM(Game::US);
    CHECK_FALSE(run(Game::US, start_game()).is_cheating);
    // a password game: Down on the title (up from frame 100 or so) selects PASSWORD
    Script pw;
    pw.frames = 400;
    pw.presses = {{150, BTN_DOWN}, {200, BTN_START}};
    Result r = run(Game::US, pw);
    CHECK(r.ram[ScreenId] == 2);               // the password screen
    CHECK(r.is_cheating);
    // a debug option
    Script poke = start_game();
    poke.pokes.push_back({100, 0x7FF, 0});
    CHECK(run(Game::US, poke).is_cheating);
}

// Presses that type `text` on the password screen and pick END. The screen is a grid of 7 rows x 10 columns
// holding the characters with codes 0-69 (row*10 + column); the font tile of code c is c + 4 (b4_PasswordCharTiles),
// so 0-9 are digits, A-Z 10-35, a-z 36-61, 62 is tile $42, then - . , ! ? and the quotes (63-69). The cursor starts at the top left; the
// d-pad wraps, so it is only ever moved straight towards the next character (no wrapping), and A types it.
static std::vector<Press> type_password(const std::string& text, uint64_t first_frame, uint64_t step = 16) {
    static const std::string order = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz~-.,!?";   // (~ = tile $42, no character in the text table)
    std::vector<Press> out;
    uint64_t f = first_frame;
    int row = 0, col = 0;
    auto press = [&](uint8_t b) { out.push_back({f, b}); f += step; };
    for (char ch : text) {
        if (ch == ' ') continue;               // the gaps are only drawn
        const int code = int(order.find(ch));
        REQUIRE(code >= 0);
        for (; row < code / 10; row++) press(BTN_DOWN);
        for (; row > code / 10; row--) press(BTN_UP);
        for (; col < code % 10; col++) press(BTN_RIGHT);
        for (; col > code % 10; col--) press(BTN_LEFT);
        press(BTN_A);
    }
    press(BTN_A);                              // after the 9th character the cursor jumps to END
    return out;
}

TEST_CASE("a password game is cheating and is not logged", "[game][trainers][leaderboards]") {
    REQUIRE_ROM(Game::US);
    Options o;
    o.leaderboards_path = "/nonexistent-dir/leaderboards.ini";   // leaderboards on, as by default
    // Down + Start on the title opens the password screen; then a valid Round 5 password is typed.
    Script pw;
    pw.presses = {{150, BTN_DOWN}, {200, BTN_START}};   // the password screen is up well before frame 400
    auto typed = type_password("BQ! TjH SnW", 400);
    pw.presses.insert(pw.presses.end(), typed.begin(), typed.end());
    const uint64_t typed_end = typed.back().frame;
    // END is checked, the screen fades out, and ROUND 5 START waits for A or Start: A every so often
    // until then (in the round it is only a jump)
    for (uint64_t f = typed_end + 300; f < typed_end + 1000; f += 40) pw.presses.push_back({f, BTN_A});
    pw.frames = typed_end + 1200;
    Result r = run(Game::US, pw, o);
    CHECK(r.ram[RoundNumber] == 4);            // the password was accepted: round 5
    CHECK(r.ram[GameState] == 5);              // and the game is on
    CHECK(r.is_cheating);
    CHECK_FALSE(r.nes->lb.run);                // no time or score is logged for this game
    // the same start without the password is a normal, logged game
    Result plain = run(Game::US, start_game(), o);
    CHECK_FALSE(plain.is_cheating);
    CHECK(plain.nes->lb.run);
}

TEST_CASE("--ending goes straight to the ending story", "[game][trainers]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Script s = start_game(400);
        CHECK(run(g, s).ram[ScreenId] == 3);   // the intro story
        Options o;
        o.ending = true;
        Result r = run(g, s, o);
        CHECK(r.ram[ScreenId] == 9);
        CHECK(r.is_cheating);
    }
}


// Mark Round 6 Doors: start on round 6, walk left to the door at tile 5 (door #3, which leads to #6),
// go through it, then straight back through #6. Both doors are then marked, the door drawn where #3
// stands is the recoloured one, and the game itself is the same as without the trainer.
TEST_CASE("Mark Round 6 Doors", "[game][trainers]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        auto through_doors = [g](bool mark) {
            Options o;
            o.trainers.start_round = 6;
            o.trainers.mark_doors = mark;
            Script s = start_game(3200);
            int phase = 0;                             // 0 walk to door #3, 1 wait for the warp, 2 go back, 3 wait, 4 done
            uint64_t busy_until = 0, settled = 0;
            s.act = [&](Nes& n) {
                const uint8_t* ram = n.cpu.ram;
                const uint64_t f = n.frame_count;
                if (f < 1000 || ram[GameState] != 5) return;
                if (ram[GameMode]) { settled = 0; if (phase == 1) phase = 2; return; }
                if (f < busy_until) return;
                if (++settled < 30) return;            // a room has just come in
                if (phase == 3 && f > busy_until + 30) { s.frames = f + 1; phase = 4; return; }   // back at door #3: stop there
                const int wx = (ram[ScreenPage] + ram[ObjXHi]) * 256 + ram[ObjX] + ram[ScrollX];
                if (phase == 0 && wx > 94) { s.presses.push_back({f + 1, BTN_LEFT}); busy_until = f + 9; }
                else if (phase == 0 || phase == 2) { s.presses.push_back({f + 1, BTN_UP}); busy_until = f + 40; phase++; }
            };
            Result r = run(g, s, o);
            CHECK(phase == 4);
            return r;
        };
        Result plain = through_doors(false);
        Result r = through_doors(true);
        INFO((g == Game::US ? "US" : "prototype"));
        CHECK(r.ram[RoomId] == ROUND6_ROOM);
        CHECK(r.nes->doors_entered == ((uint64_t(1) << 3) | (uint64_t(1) << 6)));
        CHECK(plain.nes->doors_entered == 0);
        CHECK(r.ram_hash == plain.ram_hash);           // drawing only
        // door #3 (row 10, x tile 5 of page 0) is on screen: its top half's white band (the PNG's
        // 3rd row) in the marked door's white, which the plain run's door doesn't have there
        const Nes& n = *r.nes;
        const int cam = n.room.cam_x, x = 5 * 16 + 4 - cam, y = 9 * 16 + 2;
        REQUIRE(x >= 0);
        REQUIRE(x < 256);
        const uint8_t white = custom_sprite::R6_MARKED_DOOR.colours[2];
        CHECK(n.frame[y * n.frame_w + x] == n.rgb[white]);
        int marked = 0, plain_marked = 0;
        for (int yy = 9 * 16; yy < 11 * 16; yy++)
            for (int xx = 5 * 16 - cam; xx < 6 * 16 - cam; xx++) {
                const uint8_t mid = custom_sprite::R6_MARKED_DOOR.colours[1];
                marked += n.frame[yy * n.frame_w + xx] == n.rgb[mid];
                plain_marked += plain.nes->frame[yy * plain.nes->frame_w + xx] == plain.nes->rgb[mid];
            }
        CHECK(marked > 50);
        CHECK(plain_marked < marked);
    }
}

TEST_CASE("Mark Round 6 Doors: a new game clears the marks, a continue keeps them", "[trainers]") {
    static Nes nes;                                    // (no ROM needed: only the hooks are called)
    nes.cpu.nes = &nes;
    nes.trainers.mark_doors = true;
    Cpu& c = nes.cpu;
    c.ram[RoomId] = ROUND6_ROOM;
    c.Y = 2 * 54; c.trainer_door_entered();            // the boss door counts too
    c.Y = 2 * 3; c.trainer_door_entered();
    CHECK(nes.doors_entered == ((uint64_t(1) << 54) | (uint64_t(1) << 3)));
    c.ram[RoomId] = 1;                                 // another round's room: not noted
    c.Y = 2 * 1; c.trainer_door_entered();
    CHECK(nes.doors_entered == ((uint64_t(1) << 54) | (uint64_t(1) << 3)));
    c.A = 0x0C; c.trainer_start_health(0x0C);          // a continue after Game Over
    CHECK(nes.doors_entered != 0);
    c.Y = 3; c.trainer_title_done();                   // a new game
    CHECK(nes.doors_entered == 0);
    nes.trainers.mark_doors = false;                   // trainer off: nothing is noted
    c.ram[RoomId] = ROUND6_ROOM;
    c.Y = 2 * 3; c.trainer_door_entered();
    CHECK(nes.doors_entered == 0);
}
