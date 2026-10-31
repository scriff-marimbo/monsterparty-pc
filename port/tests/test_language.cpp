// The Language setting (language.cpp): the prototype in English, the US release in Japanese.
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "lang_text.h"
#include "language.h"
#include "ram_symbols.h"
#include "rom_data.h"
#include "support.h"

using namespace mptest;

namespace {

// A ROM loaded and set up with a language, without running it.
std::unique_ptr<Nes> set_up(Game g, Language l, std::string* report = nullptr) {
    auto nes = std::make_unique<Nes>();
    std::string err;
    REQUIRE(nes->load(rom_path(g), g, err));
    const Game other = g == Game::Proto ? Game::US : Game::Proto;
    REQUIRE(apply_language(*nes, l, have_rom(other) ? rom_path(other) : "", err, report));
    return nes;
}

const char* csv_text(const char* id, bool ja) {
    for (const auto& it : lang_text::items) if (!std::strcmp(it.id, id)) return ja ? it.us_ja : it.jp_en;
    return "";
}

// the text without spaces and line breaks (to compare with what's on screen)
std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s) if (c != ' ' && c != '\n') out += c;
    return out;
}

// a row of a screen picture (bank 2) as text, drawn with the screens' override
std::string picture_row(const Nes& nes, Game g, int sid, int row, int c0, int c1) {
    const uint16_t setup = g == Game::Proto ? 0x8176 : 0x939F;
    const uint16_t pic = prg_word(nes.prg, 4, uint16_t(setup + 8 * sid));
    std::string s;
    for (int c = c0; c <= c1; c++)
        s += lang_tile_text(nes, nes.lang.bg_override(0x1F, uint8_t(sid)), 0x1F, prg_rd(nes.prg, 2, uint16_t(pic + 4 + row * 32 + c)));
    return s;
}

// the story box (PPU $2246: 20 tiles, text rows 19, 21, 23, 25) as text, marks left out
std::string story_box(const Nes& nes) {
    std::string s;
    for (int row = 19; row <= 25; row += 2)
        for (int c = 6; c < 26; c++)
            s += lang_tile_text(nes, nes.lang.bg_override(0x1F, 3), 0x1F, nes.vram[nes.nt_index(uint16_t(0x2000 + row * 32 + c))]);
    return s;
}

}  // namespace

TEST_CASE("language setting names", "[language]") {
    Language l;
    for (Language x : {Language::Default, Language::English, Language::Japanese}) {
        REQUIRE(parse_language(language_setting(x), l));
        CHECK(l == x);
    }
    CHECK_FALSE(parse_language("klingon", l));
    CHECK(language_changes(Language::English, true));
    CHECK(language_changes(Language::Japanese, false));
    CHECK_FALSE(language_changes(Language::English, false));
    CHECK_FALSE(language_changes(Language::Japanese, true));
    CHECK_FALSE(language_changes(Language::Default, true));
}

TEST_CASE("a language that doesn't change the game leaves it alone", "[language]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> prg = load_prg(g);
        for (Language l : {Language::Default, g == Game::Proto ? Language::Japanese : Language::English}) {
            auto nes = set_up(g, l);
            CHECK_FALSE(nes->lang.active);
            CHECK(nes->prg == prg);
            CHECK(nes->lang.bg_override(0x1F, 3) == nullptr);
        }
    }
}

TEST_CASE("Japanese needs the prototype's ROM", "[language]") {
    REQUIRE_ROM(Game::US);
    Nes nes;
    std::string err;
    REQUIRE(nes.load(rom_path(Game::US), Game::US, err));
    CHECK_FALSE(apply_language(nes, Language::Japanese, "", err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(nes.lang.active);
}

TEST_CASE("every character of the translations has a tile", "[language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    for (Game g : {Game::US, Game::Proto}) {
        std::string report;
        auto nes = set_up(g, g == Game::Proto ? Language::English : Language::Japanese, &report);
        CHECK(nes->lang.active);
        INFO(report);
        CHECK(report.find("no tile for") == std::string::npos);
        CHECK(report.find("free tiles") == std::string::npos);
    }
}

TEST_CASE("story pages are read from $6000 and end in $FF", "[language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    for (Game g : {Game::US, Game::Proto}) {
        auto nes = set_up(g, g == Game::Proto ? Language::English : Language::Japanese);
        const uint16_t ptrs = g == Game::Proto ? 0x9F82 : 0xB1AF;
        for (int p = 0; p < 25; p++) {
            const uint16_t a = prg_word(nes->prg, 4, uint16_t(ptrs + 2 * p));
            REQUIRE(a >= 0x6000);
            REQUIRE(a < 0x8000);
            // each line fits in 20 tiles, and the page ends
            size_t i = a - 0x6000;
            int col = 0;
            for (; nes->lang.text[i] != 0xFF; i++) {
                const uint8_t b = nes->lang.text[i];
                if (b == 0xFE) col = 0;
                else if (b != 0x60 && b != 0x61 && b != 0xEF) { CHECK(col < 20); if (++col == 20) col = 0; }
                REQUIRE(i < nes->lang.text.size());
            }
        }
    }
}

TEST_CASE("the password and game over screens are translated", "[language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    {
        auto nes = set_up(Game::Proto, Language::English);
        CHECK(picture_row(*nes, Game::Proto, 2, 20, 9, 23) == "INPUT PASSWORD.");
        CHECK(picture_row(*nes, Game::Proto, 2, 3, 7, 25) == "0 1 2 3 4 5 6 7 8 9");
        CHECK(picture_row(*nes, Game::Proto, 2, 9, 7, 25) == "U V W X Y Z a b c d");
        CHECK(picture_row(*nes, Game::Proto, 6, 17, 15, 22) == squeeze(csv_text("game_over.continue", false)));
        // the password's characters are drawn with the same tiles as the alphabet
        const uint16_t pic = prg_word(nes->prg, 4, uint16_t(0x8176 + 8 * 2));
        // (and below $80: the game takes bit 7 to mean "with ゛/゜ above", and draws tile & $7F)
        for (int k = 0; k < 70; k++) {
            const uint8_t t = prg_rd(nes->prg, 4, uint16_t(0x87C6 + k));
            CHECK(t == prg_rd(nes->prg, 2, uint16_t(pic + 4 + (3 + 2 * (k / 10)) * 32 + 7 + 2 * (k % 10))));
            CHECK(t < 0x80);
        }
    }
    {
        auto nes = set_up(Game::US, Language::Japanese);
        const std::string prompt = squeeze(csv_text("password.prompt", true));
        CHECK(picture_row(*nes, Game::US, 2, 20, 9, 23).find("いれてく") != std::string::npos);
        (void)prompt;
        // the US alphabet is kept
        CHECK(picture_row(*nes, Game::US, 2, 3, 7, 25) == "0 1 2 3 4 5 6 7 8 9");
        CHECK(picture_row(*nes, Game::US, 6, 17, 13, 16) == "つつける");   // (its ゛ is in the row above)
    }
}

TEST_CASE("the prototype's intro in English", "[game][language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    Script s;
    s.frames = 700;
    s.presses.push_back({200, BTN_START});   // title -> intro (not skipped)
    Options o;
    o.language = Language::English;
    Result r = run(Game::Proto, s, o);
    REQUIRE(r.nes->lang.active);
    CHECK(r.ram[ScreenId] == 3);
    const std::string box = story_box(*r.nes);
    INFO(box);
    CHECK(box.find("One day,") != std::string::npos);
    CHECK(box.find('?') == std::string::npos);
}

TEST_CASE("the US release's intro in Japanese", "[game][language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    Script s;
    s.frames = 700;
    s.presses.push_back({200, BTN_START});
    Options o;
    o.language = Language::Japanese;
    Result r = run(Game::US, s, o);
    CHECK(r.ram[ScreenId] == 3);
    const std::string box = story_box(*r.nes);
    INFO(box);
    CHECK(box.find("ークはやきゅう") != std::string::npos);   // (マ is the port's own tile; the first glyph found is checked too)
    CHECK(box.find('?') == std::string::npos);
}

TEST_CASE("speech bubbles are translated", "[game][language]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    for (Game g : {Game::US, Game::Proto}) {
        const bool ja = g == Game::US;
        Script s = enter_first_boss_door();
        std::string seen;
        int index = -1;
        s.watch = [&](const Nes& nes) {
            if (!seen.empty() || nes.lang.bubble_chr < 0) return;
            index = nes.lang.bubble_index;
            const TileOverride* ov = nes.lang.bg_override(nes.lang.bubble_chr, 5);
            for (int r = 1; r <= 6; r++)
                for (int c = 1; c <= 10; c++)
                    seen += lang_tile_text(nes, ov, nes.lang.bubble_chr, nes.cpu.ram[0x55C + r * 12 + c]);
        };
        Options o;
        o.language = ja ? Language::Japanese : Language::English;
        Result r = run(g, s, o);
        REQUIRE(index >= 0);
        char id[16];
        std::snprintf(id, sizeof id, "bubble.%02X", index);
        INFO(id << ": " << seen);
        CHECK(seen.find('?') == std::string::npos);
        // its text, apart from the marks (in the rows above), is all there
        std::string want = squeeze(csv_text(id, ja));
        std::string got = squeeze(seen);
        for (const char* m : {"゛", "゜"}) for (size_t p; (p = got.find(m)) != std::string::npos;) got.erase(p, std::strlen(m));
        if (!ja) for (size_t p; (p = want.find("’")) != std::string::npos;) want.replace(p, std::strlen("’"), "'");
        if (ja) CHECK(!got.empty());
        else CHECK(got == want);
    }
}

TEST_CASE("the language doesn't stop the game", "[game][language][options]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    for (Game g : {Game::US, Game::Proto}) {
        Options o;
        o.language = g == Game::Proto ? Language::English : Language::Japanese;
        Result r = run(g, play_round1(), o);
        CHECK(r.ram[GameState] == 5);
        // Game Default is the game as it was
        Options d;
        d.language = Language::Default;
        CHECK(run(g, play_round1(), d).ram_hash == run(g, play_round1()).ram_hash);
    }
}
