// Monster Party native port: the Language setting (not in the original games).
//
// Game Default plays each ROM with its own text. English or 日本語 shows both games in that
// language: the prototype in English, or the US release in Japanese (the other combinations are
// already in that language and don't change). The text is the "JP ROM EN" and "US ROM JA" columns
// of tools/text/text_catalogue.csv (lang_text.h, made by tools/make_lang_text.py).
//
// It's drawing only, and the games' own font is used: each character's tile comes from whichever
// ROM has it (Latin from the US release, kana from the prototype, so 日本語 needs the prototype's
// ROM), plus a few the port draws itself (graphics/font_extra.png). The text is written with tile
// codes chosen for it, and the renderer draws those codes with the borrowed tiles (TileOverride,
// background only, in a copy: the ROM's CHR isn't changed):
//   - story text box (ScreenId 3 intro, 9 ending): the pages are new byte streams the game reads
//     from $6000-$7FFF (Cpu::rd), b4_StoryPagePtrs pointing there; the game's own text engine types,
//     wraps and scrolls them (20 tiles a line, 4 lines shown; ゛/゜ in the row above, codes $60/$61)
//   - the password, between-rounds and game over screens: their nametable pictures (bank 2) and the
//     TRY AGAIN box (bank 6) are edited in the loaded PRG copy, like the Remix edits its data
//   - speech bubbles: the 12 x 8 tile grid the game copies to RAM $055C is rewritten as it's
//     shown (LANG_HOOKS in translate.py, Cpu::lang_bubble_*), with that arena's bubble-font codes
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class Language : uint8_t { Default, English, Japanese };
const char* language_setting(Language l);                 // "default", "en", "ja" (settings and --language)
bool parse_language(const std::string& s, Language& out);
// whether the setting changes that game's text: English on the prototype, 日本語 on the US release
bool language_changes(Language l, bool proto);

// Background tiles drawn instead of the ROM's, for one 4 KB CHR bank (null = the ROM's own tile).
struct TileOverride { const uint8_t* t[256] = {}; };

struct LangRuntime {
    bool active = false;                       // this game's text is replaced
    bool japanese = false;                     // the US release in Japanese (else the prototype in English)
    std::vector<std::array<uint8_t, 16>> glyphs;   // tile bitmaps the overrides point at (never resized after setup)
    std::vector<std::string> glyph_chars;      // the character each glyph is (UTF-8), for tests and reports
    TileOverride story, screens, bubble;
    int bubble_chr = -1;                       // CHR bank the bubble override is for (-1: none)
    std::array<uint8_t, 0x2000> text{};        // the story pages, read by the game at $6000-$7FFF
    // speech bubbles: each page's 10 x 6 inside, as glyph numbers (-1 blank), key index * 8 + page
    std::map<int, std::array<int16_t, 60>> bubbles;
    std::map<int, std::vector<uint8_t>> bubble_codes;   // CHR bank -> its bubble-font codes (may be redrawn)
    std::map<int, std::vector<uint8_t>> bubble_spare;   // CHR bank -> blank codes, if those run out
    int bubble_index = -1, bubble_page = 0;    // the bubble being shown (index = RoundNumber*4 + BossIdInRound)
    // the background tiles to draw for CHR bank `bank` on screen `screen_id`, or null
    const TileOverride* bg_override(int bank, uint8_t screen_id) const {
        if (!active) return nullptr;
        if (bank == 0x1F) return (screen_id == 3 || screen_id == 9) ? &story : &screens;
        return bank == bubble_chr ? &bubble : nullptr;
    }
};

struct Nes;
// Sets up the Language for the loaded ROM: after Nes::load (and the Bosses/Remix edits), before
// reset. Edits nes.prg; other_rom = the other game's ROM file (for its font). Nothing happens if the
// setting doesn't change this game. err: why it can't be done (e.g. 日本語 without the prototype).
// report (optional): every text as it will be laid out, and anything that doesn't fit.
bool apply_language(Nes& nes, Language l, const std::string& other_rom, std::string& err, std::string* report = nullptr);
// What a background tile shows, as text: tile `code` of CHR bank `bank` as drawn with `ov` (null =
// the ROM's own tile). " " for the blank tile, "?" for a tile that isn't one of the text's glyphs.
std::string lang_tile_text(const Nes& nes, const TileOverride* ov, int bank, uint8_t code);
