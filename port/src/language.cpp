// Monster Party native port: the Language setting. See language.h for how it works.
#include "language.h"
#include "nes.h"
#include "lang_text.h"
#include "font_extra.h"
#include "ram_symbols.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <set>

const char* language_setting(Language l) {
    return l == Language::English ? "en" : l == Language::Japanese ? "ja" : "default";
}

bool parse_language(const std::string& s, Language& out) {
    if (s == "default" || s.empty()) out = Language::Default;
    else if (s == "en" || s == "english") out = Language::English;
    else if (s == "ja" || s == "japanese") out = Language::Japanese;
    else return false;
    return true;
}

bool language_changes(Language l, bool proto) {
    return proto ? l == Language::English : l == Language::Japanese;
}

namespace {

// ---------------------------------------------------------------- where things are in each ROM
struct Addrs {
    uint16_t story_ptrs;                       // bank 4: b4_StoryPagePtrs (25 pages)
    uint16_t screen_setup;                     // bank 4: b4_ScreenSetupTable (8 bytes per ScreenId)
    uint16_t pw_tiles;                         // bank 4: b4_PasswordCharTiles (grid position -> tile)
    uint16_t try_box, try_restore;             // bank 6: the TRY AGAIN box (12 x 7), and what it covers
    uint16_t bubble_lo, bubble_hi;             // bank 2: speech bubble pointers by RoundNumber*4 + BossIdInRound
    uint16_t room_chr1;                        // bank 7: RoomChr1Banks (per RoomId; arenas are 8-15)
};
const Addrs US_ADDRS{0xB1AF, 0x939F, 0x9A05, 0xA596, 0xA63E, 0x9CF8, 0x9D19, 0xDAAB};
const Addrs JP_ADDRS{0x9F82, 0x8176, 0x87C6, 0xA184, 0xA22C, 0x9CE6, 0x9D07, 0xD996};
constexpr int STORY_PAGES = 25, BUBBLES = 33, GRID = 70;
constexpr uint8_t BLANK = 0x01, FRAME = 0x7F;   // the font's blank tile, and the boxes' border (bank $1F)

size_t at(int bank, uint16_t addr) { return size_t(bank) * 0x4000 + (addr & 0x3FFF); }

// ---------------------------------------------------------------- text
std::u32string utf32(const char* s) {
    std::u32string out;
    for (const unsigned char* p = (const unsigned char*)s; *p;) {
        char32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (more) c &= 0x3F >> more;
        for (; more && (*p & 0xC0) == 0x80; more--) c = c << 6 | (*p++ & 0x3F);
        out += c;
    }
    return out;
}

std::string utf8(char32_t c) {
    std::string s;
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | c >> 6); s += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += char(0xE0 | c >> 12); s += char(0x80 | (c >> 6 & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    else { s += char(0xF0 | c >> 18); s += char(0x80 | (c >> 12 & 0x3F)); s += char(0x80 | (c >> 6 & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    return s;
}

// Kana with ゛ or ゜: the base kana and the mark (the game draws the mark in the row above)
constexpr char32_t DAKUTEN = U'゛', HANDAKUTEN = U'゜';
bool split_mark(char32_t c, char32_t& base, char32_t& mark) {
    static const std::u32string voiced = U"がぎぐげござじずぜぞだぢづでどばびぶべぼゔガギグゲゴザジズゼゾダヂヅデドバビブベボヴ";
    static const std::u32string plain_ = U"かきくけこさしすせそたちつてとはひふへほうカキクケコサシスセソタチツテトハヒフヘホウ";
    static const std::u32string semi = U"ぱぴぷぺぽパピプペポ";
    static const std::u32string semi_base = U"はひふへほハヒフヘホ";
    if (size_t i = voiced.find(c); i != std::u32string::npos) { base = plain_[i]; mark = DAKUTEN; return true; }
    if (size_t i = semi.find(c); i != std::u32string::npos) { base = semi_base[i]; mark = HANDAKUTEN; return true; }
    return false;
}

// One tile of laid-out text: the glyph (-1 = blank), the mark above it (-1 none), and the
// character it came from (for the report).
struct Cell { int glyph = -1, mark = -1; char32_t ch = U' '; };
using Line = std::vector<Cell>;

// ---------------------------------------------------------------- the font
// Where each character's tile is: the US release's font (Latin), the prototype's (kana), or the
// port's own (font_extra.h). bank/tile are CHR bank and tile number in that ROM.
enum Rom : uint8_t { US_ROM, JP_ROM, EXTRA };
struct Source { Rom rom; uint8_t bank, tile; };

// private-use characters for the US password screen's own SPACE and BACK words (3 tiles each)
constexpr char32_t LIG_SPACE = 0xE000, LIG_BACK = 0xE003;

bool find_source(char32_t c, Source& s) {
    auto us = [&](uint8_t t) { s = {US_ROM, 0x1F, t}; return true; };
    auto jp = [&](uint8_t bank, uint8_t t) { s = {JP_ROM, bank, t}; return true; };
    if (c >= U'0' && c <= U'9') return us(uint8_t(0x04 + c - U'0'));
    if (c >= U'A' && c <= U'Z') return us(uint8_t(0x0E + c - U'A'));
    if (c >= U'a' && c <= U'z') return us(uint8_t(0x28 + c - U'a'));
    static const std::u32string us_punct = U"•-.,!?“”'";
    if (size_t i = us_punct.find(c); i != std::u32string::npos) return us(uint8_t(0x42 + i));
    if (c >= LIG_SPACE && c < LIG_SPACE + 6) return us(uint8_t(0x60 + c - LIG_SPACE));
    // the prototype's font (bank $1F), in the order of its tiles
    static const std::u32string hira = U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん";
    if (size_t i = hira.find(c); i != std::u32string::npos) return jp(0x1F, uint8_t(0x32 + i));
    static const std::u32string jp1 = U"゛゜ゃゅょっー！？";
    if (size_t i = jp1.find(c); i != std::u32string::npos) return jp(0x1F, uint8_t(0x60 + i));
    static const std::u32string jp2 = U"イオキソコタチトフモラリレロンッュ、。・";
    if (size_t i = jp2.find(c); i != std::u32string::npos) return jp(0x1F, uint8_t(0xB0 + i));
    switch (c) {
    case U'ハ': return jp(0x1F, 0x7A);
    case U'ス': return jp(0x1F, 0x7B);
    case U'ワ': return jp(0x1F, 0x7C);
    case U'「': return jp(0x1F, 0xC6);
    case U'」': return jp(0x1F, 0xC7);
    case U'ぃ': return jp(0x1F, 0xC8);
    // katakana and signs only in the prototype's speech-bubble fonts (arena banks)
    case U'♥': return jp(0x0B, 0xFF);
    case U'♪': return jp(0x09, 0xFF);
    case U'♯': return jp(0x09, 0xFE);
    case U'ァ': return jp(0x15, 0xFB);
    case U'ア': return jp(0x0F, 0xF1);
    case U'エ': return jp(0x0D, 0xF7);
    case U'ク': return jp(0x0F, 0xF3);
    case U'シ': return jp(0x0F, 0xF4);
    case U'ツ': return jp(0x0F, 0xF5);
    case U'テ': return jp(0x13, 0xF3);
    case U'ヒ': return jp(0x0F, 0xF7);
    case U'ホ': return jp(0x13, 0xF5);
    case U'ム': return jp(0x13, 0xF6);
    case U'ャ': return jp(0x0F, 0xFA);
    case U'ヤ': return jp(0x0D, 0xFA);
    case U'ヨ': return jp(0x0F, 0xF8);
    case U'ル': return jp(0x13, 0xF8);
    }
    for (size_t i = 0; i < std::size(font_extra::glyphs); i++)
        if (utf32(font_extra::glyphs[i].ch) == std::u32string(1, c)) { s = {EXTRA, 0, uint8_t(i)}; return true; }
    if (c == U'ヘ') return find_source(U'へ', s);   // the same shape
    return false;
}

// ---------------------------------------------------------------- choosing tile codes
// A set of tile codes in one CHR bank that text can be drawn with. Codes whose tile already shows
// the glyph are used as they are; others get the glyph drawn over them (in the override).
struct Alloc {
    enum State : uint8_t { FREE, IN_USE, NEVER };
    const uint8_t* bank;                       // the running ROM's tiles
    TileOverride* ov;
    std::array<State, 256> state{};
    std::array<int, 256> owner;                // glyph shown at this code, -1 none
    std::vector<uint8_t> order;                // free codes, in the order to use them
    std::map<int, int> code_of;
    bool full = false;
    Alloc(const uint8_t* b, TileOverride* o, State all) : bank(b), ov(o) { state.fill(all); owner.fill(-1); }
    void make_free(uint8_t c) { if (state[c] != NEVER) { state[c] = FREE; order.push_back(c); } }
    // limit: only codes below it (the password alphabet's must be below $80: bit 7 of a password
    // tile means "with a mark above" to the game)
    void assign(const std::vector<int>& glyphs, const std::vector<std::array<uint8_t, 16>>& store, int limit = 256) {
        std::vector<int> todo;
        for (int g : glyphs) if (g >= 0 && !code_of.count(g) && std::find(todo.begin(), todo.end(), g) == todo.end()) todo.push_back(g);
        std::vector<int> rest;
        for (int g : todo) {                   // already in the bank (same picture): use it
            int found = -1;
            for (int c = 0; c < limit && found < 0; c++)
                if (state[c] != NEVER && owner[c] < 0 && !(ov && ov->t[c]) && std::memcmp(bank + c * 16, store[g].data(), 16) == 0) found = c;
            if (found >= 0) { code_of[g] = found; owner[found] = g; }
            else rest.push_back(g);
        }
        size_t next = 0;
        for (int g : rest) {
            while (next < order.size() && (owner[order[next]] >= 0 || state[order[next]] != FREE || order[next] >= limit)) next++;
            if (next == order.size()) { full = true; continue; }
            const uint8_t c = order[next++];
            code_of[g] = c; owner[c] = g;
            if (ov) ov->t[c] = store[g].data();
        }
    }
    int code(int g) const { auto it = code_of.find(g); return it == code_of.end() ? -1 : it->second; }
};

bool uniform(const uint8_t* t) {   // every pixel the same colour (a blank tile)
    for (int y = 0; y < 8; y++) {
        if (t[y] != 0 && t[y] != 0xFF) return false;
        if (t[y + 8] != 0 && t[y + 8] != 0xFF) return false;
        if (t[y] != t[0] || t[y + 8] != t[8]) return false;
    }
    return true;
}

// ---------------------------------------------------------------- setting it all up
struct Setup {
    Nes& nes;
    LangRuntime& L;
    const Addrs& A;
    bool ja;                                   // the text is Japanese (the US release)
    const std::vector<uint8_t>* chr_of[2];     // US, JP CHR
    const std::vector<uint8_t>* us_prg = nullptr;   // the US release's PRG (the report shows its own bubbles)
    std::map<char32_t, int> glyph_of;
    std::vector<char32_t> glyph_char;
    std::vector<std::string> problems;
    std::string rep;
    bool reporting;
    bool raw = false;                          // cells(): take the characters as they are (the password alphabet)

    const char* text(const char* id) const {
        for (const auto& it : lang_text::items)
            if (!std::strcmp(it.id, id)) return ja ? it.us_ja : it.jp_en;
        return nullptr;
    }
    void problem(const std::string& where, const std::string& what) { problems.push_back(where + ": " + what); }

    int glyph(char32_t c, const std::string& where) {
        if (auto it = glyph_of.find(c); it != glyph_of.end()) return it->second;
        Source s;
        std::array<uint8_t, 16> t{};
        if (!find_source(c, s)) {
            problem(where, "no tile for \"" + utf8(c) + "\" in either game's font (add it to graphics/font_extra.png)");
            glyph_of[c] = -1;
            return -1;
        }
        if (s.rom == EXTRA) std::memcpy(t.data(), font_extra::glyphs[s.tile].tile, 16);
        else std::memcpy(t.data(), chr_of[s.rom]->data() + s.bank * 0x1000 + s.tile * 16, 16);
        const int g = int(L.glyphs.size());
        L.glyphs.push_back(t);
        L.glyph_chars.push_back(utf8(c));
        glyph_char.push_back(c);
        glyph_of[c] = g;
        return g;
    }

    // The text of one paragraph as cells, normalised for the language's font.
    Line cells(const std::u32string& in, const std::string& where) {
        std::u32string s;
        for (size_t i = 0; i < in.size(); i++) {
            char32_t c = in[i];
            if (c == U'　') c = U' ';
            if (ja) {
                if (c == U'!') c = U'！';
                else if (c == U'?') c = U'？';
                s += c;
            } else {
                if (c == U'’' || c == U'‘') c = U'\'';
                else if (c == U'！') c = U'!';
                else if (c == U'？') c = U'?';
                if (c == U'…') { s += U"..."; continue; }
                if (c == U'"') {   // straight quotes: open, then close
                    const bool open = std::count(s.begin(), s.end(), U'“') == std::count(s.begin(), s.end(), U'”');
                    c = open ? U'“' : U'”';
                }
                s += c;
            }
        }
        Line out;
        for (char32_t c : s) {
            Cell cell;
            cell.ch = c;
            if (c == U' ') { out.push_back(cell); continue; }
            // (a voiced kana is drawn as its plain kana with the mark above, unless the font has it whole)
            char32_t base = c, mark = 0;
            if (split_mark(c, base, mark)) {
                Source whole;
                if (find_source(c, whole)) { base = c; mark = 0; }
            }
            cell.glyph = glyph(base, where);
            if (mark) cell.mark = glyph(mark, where);
            out.push_back(cell);
        }
        return out;
    }

    // Lines of at most `width` tiles.
    // Japanese: the lines as typed in the CSV (one that's too long is broken after the last tile
    // that fits, as the game itself would). English with reflow: the CSV's lines are joined into
    // sentences and broken again between words, so each line is as full as it can be; a new line
    // still starts after a sentence's end (. ! ? ” …) or a blank line, and before an opening “, like
    // the US release's own text. Without reflow (screens) the lines are taken as typed.
    std::vector<Line> lay(const char* txt, int width, const std::string& where, bool reflow = false) {
        std::vector<std::u32string> paras;
        const std::u32string all = utf32(txt);
        for (size_t start = 0;;) {
            const size_t nl = all.find(U'\n', start);
            std::u32string line = all.substr(start, nl == std::u32string::npos ? std::u32string::npos : nl - start);
            while (!line.empty() && line.back() == U' ') line.pop_back();
            const bool join = reflow && !ja && !paras.empty() && !paras.back().empty() && !line.empty() &&
                              line[0] != U'“' && line[0] != U'"' &&
                              std::u32string(U".!?”\"…").find(paras.back().back()) == std::u32string::npos;
            if (join) paras.back() += U' ' + line;
            else paras.push_back(line);
            if (nl == std::u32string::npos) break;
            start = nl + 1;
        }
        while (paras.size() > 1 && paras.back().empty()) paras.pop_back();   // (a final line break)
        std::vector<Line> lines;
        for (const auto& para : paras) {
            Line l = cells(para, where);
            if (int(l.size()) <= width) { lines.push_back(l); continue; }
            if (ja) {
                problem(where, "\"" + show(l) + "\" is " + std::to_string(l.size()) + " tiles wide, " + std::to_string(width) + " fit: broken after the " + std::to_string(width) + "th");
                for (size_t i = 0; i < l.size(); i += width)
                    lines.push_back(Line(l.begin() + i, l.begin() + std::min(l.size(), i + size_t(width))));
                continue;
            }
            size_t i = 0;   // word wrap
            while (i < l.size()) {
                size_t end = std::min(l.size(), i + size_t(width));
                if (end < l.size()) {
                    size_t sp = end;
                    while (sp > i && l[sp].ch != U' ') sp--;
                    if (sp > i) end = sp;   // break at the space (dropped)
                    else problem(where, "a word longer than " + std::to_string(width) + " tiles is broken");
                }
                lines.push_back(Line(l.begin() + i, l.begin() + end));
                i = end;
                while (i < l.size() && l[i].ch == U' ') i++;
            }
        }
        return lines;
    }

    std::string show(const Line& l) const {
        std::string s;
        for (const Cell& c : l) s += c.ch == U' ' ? std::string(ja ? "　" : " ") : utf8(c.ch);
        return s;
    }
    void report(const std::string& s) { if (reporting) rep += s; }

    // ---------------------------------------------------------------- story pages
    void story() {
        std::vector<uint8_t> chr1f(nes.chr.begin() + 0x1F * 0x1000, nes.chr.begin() + 0x20 * 0x1000);
        Alloc a(chr1f.data(), &L.story, Alloc::FREE);
        for (int c : {0x00, int(BLANK), 0x60, 0x61, 0xEF, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}) a.state[c] = Alloc::NEVER;
        a.state[0x7E] = a.state[0x02] = Alloc::IN_USE;
        for (int c = 0; c < 256; c++) if (a.state[c] == Alloc::FREE) a.order.push_back(uint8_t(c));
        std::vector<std::vector<Line>> pages(STORY_PAGES);
        std::vector<int> need;
        report("\n---- story (20 tiles a line; the box shows 4 lines and scrolls up for more)\n");
        for (int p = 0; p < STORY_PAGES; p++) {
            char id[16];
            std::snprintf(id, sizeof id, "story.%02d", p);
            const char* t = text(id);
            if (!t || !*t) { problem(id, "no text (the page is left empty)"); t = ""; }
            pages[p] = lay(t, 20, id, true);
            report(std::string(id) + "\n");
            for (const Line& l : pages[p]) {
                report("  |" + show(l) + "|\n");
                for (const Cell& c : l) { need.push_back(c.glyph); need.push_back(c.mark); }
            }
        }
        // the marks are the codes $60 and $61 themselves (the text engine's own mark codes)
        const int dak = ja ? glyph(DAKUTEN, "story") : -1, han = ja ? glyph(HANDAKUTEN, "story") : -1;
        need.erase(std::remove(need.begin(), need.end(), dak), need.end());
        need.erase(std::remove(need.begin(), need.end(), han), need.end());
        a.assign(need, L.glyphs);
        if (a.full) problem("story", "too many different characters for the font's free tiles");
        if (dak >= 0 && std::memcmp(chr1f.data() + 0x60 * 16, L.glyphs[dak].data(), 16)) L.story.t[0x60] = L.glyphs[dak].data();
        if (han >= 0 && std::memcmp(chr1f.data() + 0x61 * 16, L.glyphs[han].data(), 16)) L.story.t[0x61] = L.glyphs[han].data();
        // the byte streams, at $6000 on
        size_t off = 0;
        for (int p = 0; p < STORY_PAGES; p++) {
            std::vector<uint8_t> b;
            for (const Line& l : pages[p]) {
                for (const Cell& c : l) {
                    if (c.mark >= 0) b.push_back(c.mark == dak ? 0x60 : 0x61);
                    const int code = c.glyph < 0 ? BLANK : a.code(c.glyph);
                    b.push_back(uint8_t(code < 0 ? BLANK : code));
                }
                if (l.size() < 20) b.push_back(0xFE);   // (a full line wraps by itself)
            }
            if (!b.empty() && b.back() == 0xFE) b.back() = 0xFF;
            else b.push_back(0xFF);
            if (off + b.size() > L.text.size()) { problem("story", "too much text"); break; }
            std::copy(b.begin(), b.end(), L.text.begin() + off);
            const uint16_t ptr = uint16_t(0x6000 + off);
            nes.prg[at(4, uint16_t(A.story_ptrs + 2 * p))] = uint8_t(ptr);
            nes.prg[at(4, uint16_t(A.story_ptrs + 2 * p + 1))] = uint8_t(ptr >> 8);
            off += b.size();
        }
    }

    // ---------------------------------------------------------------- screens
    // a screen's nametable picture (bank 2): 32 x 30 tiles
    uint8_t& cell(int sid, int row, int col) {
        const size_t e = at(4, uint16_t(A.screen_setup + 8 * sid));
        const uint16_t pic = uint16_t(nes.prg[e] | nes.prg[e + 1] << 8);
        return nes.prg[at(2, uint16_t(pic + 4 + row * 32 + col))];
    }
    uint8_t& box(uint16_t addr, int row, int col) { return nes.prg[at(6, uint16_t(addr + row * 12 + col))]; }

    struct Put { std::function<uint8_t&(int, int)> at; int row, col; Line line; };
    std::vector<Put> puts;   // written once the codes are chosen

    void screens() {
        const uint8_t* chr1f = nes.chr.data() + 0x1F * 0x1000;
        Alloc a(chr1f, &L.screens, Alloc::IN_USE);
        // tiles the screens use: everything in their pictures, the boxes and the password tiles
        std::set<uint8_t> used;
        for (int sid : {2, 4, 5, 6})
            for (int r = 0; r < 30; r++) for (int c = 0; c < 32; c++) used.insert(cell(sid, r, c));
        for (uint16_t b : {A.try_box, uint16_t(A.try_box + 84), A.try_restore})
            for (int i = 0; i < 84; i++) used.insert(nes.prg[at(6, uint16_t(b + i))]);
        for (int i = 0; i < GRID + 2; i++) used.insert(nes.prg[at(4, uint16_t(A.pw_tiles + i))] & 0x7F);
        // free: blank tiles from $C0 up that nothing uses ($F0/$F1 are the leaderboard screen's), and
        // (below) the tiles of the text that's replaced
        std::vector<uint8_t> high_free;
        for (int c = 0xC0; c < 0x100; c++)
            if (!used.count(uint8_t(c)) && c != 0xF0 && c != 0xF1 && uniform(chr1f + c * 16)) high_free.push_back(uint8_t(c));
        a.state[BLANK] = Alloc::NEVER;

        report("\n---- screens\n");
        // (both games have these in English, and the port doesn't change them yet)
        for (auto [id, own] : {std::pair<const char*, const char*>{"round_start", "ROUND   START"}, {"game_over", "GAME OVER"},
                               {"status_bar", "LIFE\nSCORE\nHIGH\nROUND\nKEY"}})
            if (const char* t = text(id); t && *t && std::strcmp(t, own))
                problem(id, "isn't translated by the port yet: the game's own text is shown");
        const bool proto = nes.game == Game::Proto;
        // the boxes with a title in their top edge: span = whole edge (2+ border tiles each side)
        auto framed = [&](const char* id, int sid, int row, int c0, int w) {
            const char* t = text(id);
            if (!t || !*t) return;
            std::vector<Line> ls = lay(t, 99, id);
            Line l = ls.empty() ? Line() : ls[0];
            if (ls.size() > 1) problem(id, "only one line fits");
            const int n = int(l.size());
            if (n > w - 4) problem(id, std::to_string(n) + " tiles wide, " + std::to_string(w - 4) + " fit");
            const int pad = std::max(0, w - n - (n <= w - 6 ? 2 : 0));
            const int left = pad / 2;
            for (int c = c0; c < c0 + w; c++) { cell(sid, row, c) = FRAME; cell(sid, row - 1, c) = BLANK; }
            int col = c0 + left;
            if (n <= w - 6) cell(sid, row, col++) = BLANK;
            puts.push_back({[this, sid](int r, int c) -> uint8_t& { return cell(sid, r, c); }, row, col, l});
            if (n <= w - 6) cell(sid, row, std::min(c0 + w - 1, col + n)) = BLANK;
            report(std::string(id) + ": |" + show(l) + "|\n");
        };
        // text from a column on, in a space `w` wide (cleared, with the row above for marks)
        auto left = [&](const char* id, int sid, int row, int col, int w) {
            const char* t = text(id);
            if (!t || !*t) return;
            std::vector<Line> ls = lay(t, 99, id);
            Line l = ls.empty() ? Line() : ls[0];
            if (ls.size() > 1) problem(id, "only one line fits");
            // the US password screen's own SPACE and BACK (squeezed into 3 tiles each)
            const std::string s = show(l);
            if (!ja && w == 3 && (s == "SPACE" || s == "BACK")) {
                const char32_t lig = s == "SPACE" ? LIG_SPACE : LIG_BACK;
                l.clear();
                for (int i = 0; i < 3; i++) { Cell c; c.ch = U'_'; c.glyph = glyph(lig + i, id); l.push_back(c); }
            }
            if (int(l.size()) > w) problem(id, std::to_string(l.size()) + " tiles wide, " + std::to_string(w) + " fit");
            for (int c = col; c < col + std::max(w, int(l.size())) && c < 32; c++) cell(sid, row, c) = cell(sid, row - 1, c) = BLANK;
            puts.push_back({[this, sid](int r, int c) -> uint8_t& { return cell(sid, r, c); }, row, col, l});
            report(std::string(id) + ": |" + s + "|\n");
        };
        framed("password.prompt", 2, 20, 7, 19);
        static const char* keys[] = {"password.key.ins", "password.key.del", "password.key.space", "password.key.back", "password.key.end"};
        for (int k = 0; k < 5; k++) left(keys[k], 2, 17, 7 + 4 * k, 3);
        framed("game_over.your_password", 5, 10, 8, 17);
        if (proto) {
            if (!ja) {
                // English doesn't fit the prototype's narrow menu box: widen it 2 tiles to the right
                for (int r = 16; r <= 21; r++) { cell(6, r, 21) = BLANK; cell(6, r, 23) = FRAME; }
                for (int r : {15, 22}) for (int c = 21; c <= 23; c++) cell(6, r, c) = FRAME;
            }
            left("game_over.continue", 6, 17, 15, ja ? 6 : 8);
            left("game_over.end", 6, 20, 15, ja ? 6 : 8);
        } else {
            left("game_over.continue", 6, 17, 13, 10);
            left("game_over.end", 6, 19, 13, 10);
        }
        // TRY AGAIN!: two lines in the box, the marks above them
        if (const char* t = text("password.try_again"); t && *t) {
            std::vector<Line> ls = lay(t, 10, "password.try_again");
            if (ls.size() > 2) problem("password.try_again", std::to_string(ls.size()) + " lines, 2 fit");
            for (int r = 1; r <= 5; r++) for (int c = 1; c <= 10; c++) box(A.try_box, r, c) = BLANK;
            for (size_t i = 0; i < ls.size() && i < 2; i++) {
                const int col = 1 + (10 - int(ls[i].size())) / 2;
                puts.push_back({[this](int r, int c) -> uint8_t& { return box(A.try_box, r, c); }, 2 + 2 * int(i), col, ls[i]});
                report("password.try_again: |" + show(ls[i]) + "|\n");
            }
        }
        // the password alphabet: 7 rows of 10, in the picture and in the game's table
        std::vector<Line> grid;
        if (const char* t = text("password.grid"); t && *t) {
            const std::u32string all = utf32(t);
            Line row;
            for (char32_t c : all) {
                if (c == U'\n') { if (!row.empty()) grid.push_back(row); row.clear(); continue; }
                if (c == U' ' || c == U'　') continue;
                raw = true;
                Line one = cells(std::u32string(1, c), "password.grid");
                raw = false;
                row.push_back(one[0]);
            }
            if (!row.empty()) grid.push_back(row);
            bool ok = grid.size() == 7;
            for (auto& r : grid) ok = ok && r.size() == 10;
            if (!ok) { problem("password.grid", "needs 7 rows of 10 characters"); grid.clear(); }
            for (auto& r : grid) for (auto& c : r)
                if (c.mark >= 0) { problem("password.grid", "characters with ゛/゜ aren't supported here"); c.mark = -1; }
        }

        // The tiles only the replaced text used are free now (the prototype's kana, the US release's
        // SPACE/BACK). The password alphabet gets codes below $80 from them; the rest of the text can
        // use what's left and the blank tiles from $C0.
        if (!grid.empty())
            for (int r = 0; r < 7; r++) for (int k = 0; k < 10; k++) cell(2, 3 + 2 * r, 7 + 2 * k) = cell(2, 2 + 2 * r, 7 + 2 * k) = BLANK;
        std::set<uint8_t> still;
        for (int sid : {2, 4, 5, 6})
            for (int r = 0; r < 30; r++) for (int c = 0; c < 32; c++) still.insert(cell(sid, r, c));
        for (uint16_t b : {A.try_box, uint16_t(A.try_box + 84)})
            for (int i = 0; i < 84; i++) still.insert(nes.prg[at(6, uint16_t(b + i))]);
        for (int i = 0; i < 0x6B; i++) still.insert(nes.prg[at(2, uint16_t(0x8000 + i))]);   // the status bar's text (b2_Text_Life...)
        for (int i = grid.empty() ? 0 : GRID; i < GRID + 2; i++) still.insert(nes.prg[at(4, uint16_t(A.pw_tiles + i))] & 0x7F);
        if (grid.empty()) for (int i = 0; i < 84; i++) still.insert(nes.prg[at(6, uint16_t(A.try_restore + i))]);
        for (uint8_t c : used)
            if (c >= 0x02 && c < 0x80 && !still.count(c) && !(c >= 0x28 && c <= 0x31) && c != 0x7E && c != 0x7F) a.make_free(c);
        std::vector<int> need;
        for (auto& r : grid) for (auto& c : r) need.push_back(c.glyph);
        a.assign(need, L.glyphs, 0x80);
        if (a.full) problem("password.grid", "not enough free tiles below $80 for the alphabet");
        a.full = false;
        for (uint8_t c : high_free) a.make_free(c);
        need.clear();
        for (auto& p : puts) for (auto& c : p.line) { need.push_back(c.glyph); need.push_back(c.mark); }
        a.assign(need, L.glyphs);
        if (a.full) problem("screens", "too many different characters for the font's free tiles");
        auto code = [&](int g) { const int c = g < 0 ? BLANK : a.code(g); return uint8_t(c < 0 ? BLANK : c); };
        for (auto& p : puts)
            for (size_t i = 0; i < p.line.size(); i++) {
                const int c = p.col + int(i);
                if (c >= 32) break;
                p.at(p.row, c) = code(p.line[i].glyph);
                if (p.line[i].mark >= 0) p.at(p.row - 1, c) = code(p.line[i].mark);
            }
        if (!grid.empty()) {
            for (int r = 0; r < 7; r++)
                for (int k = 0; k < 10; k++) {
                    const uint8_t t = code(grid[r][k].glyph);
                    cell(2, 3 + 2 * r, 7 + 2 * k) = t;
                    cell(2, 2 + 2 * r, 7 + 2 * k) = BLANK;
                    nes.prg[at(4, uint16_t(A.pw_tiles + r * 10 + k))] = t;   // (bit 7 clear: no mark)
                }
            // what the TRY AGAIN box covers (PPU $20CA: row 6, column 10, 12 x 7)
            for (int r = 0; r < 7; r++) for (int c = 0; c < 12; c++) box(A.try_restore, r, c) = cell(2, 6 + r, 10 + c);
            report("password.grid: from the CSV\n");
        }
    }

    // ---------------------------------------------------------------- speech bubbles
    // The inside (6 rows of 10) of the US release's own bubble `index`, page `page`, as text (its
    // bubble font: tools/text/bubble.tbl), or nothing if it has no such page.
    std::vector<std::string> us_bubble(int index, int page) const {
        if (!us_prg) return {};
        const std::vector<uint8_t>& prg = *us_prg;
        uint16_t p = uint16_t(prg[at(2, uint16_t(US_ADDRS.bubble_lo + index))] | prg[at(2, uint16_t(US_ADDRS.bubble_hi + index))] << 8);
        if (!(p >> 8)) return {};
        for (int k = 0; k < page; k++) {
            if (prg[at(2, uint16_t(p + 0x60))]) return {};   // (the last page)
            p = uint16_t(p + 0x61);
        }
        std::vector<std::string> rows;
        for (int r = 1; r <= 6; r++) {
            std::string s;
            for (int c = 1; c <= 10; c++) {
                const uint8_t t = prg[at(2, uint16_t(p + r * 12 + c))];
                if (t >= 0xD7 && t <= 0xF0) s += char('A' + t - 0xD7);
                else if (t == 0xFA) s += '.';
                else if (t == 0xFB) s += ',';
                else if (t == 0xFC) s += "·";
                else if (t == 0xFD) s += '!';
                else if (t == 0xFE) s += '\'';
                else if (t == BLANK) s += ' ';
                else s += '?';
            }
            rows.push_back(s);
        }
        return rows;
    }

    void bubbles() {
        const std::vector<uint8_t>& prg = nes.prg;
        auto arena_chr = [&](int index) { return prg[at(7, uint16_t(A.room_chr1 + 8 + std::min(index >> 2, 7)))] & 0x1F; };
        // each arena's bubble font: the tiles inside its bubbles
        std::map<int, std::set<uint8_t>> font, border;
        for (int i = 0; i < BUBBLES; i++) {
            uint16_t p = uint16_t(prg[at(2, uint16_t(A.bubble_lo + i))] | prg[at(2, uint16_t(A.bubble_hi + i))] << 8);
            if (!(p >> 8)) continue;
            for (int page = 0; page < 8; page++, p = uint16_t(p + 0x61)) {
                for (int r = 0; r < 8; r++)
                    for (int c = 0; c < 12; c++) {
                        const uint8_t t = prg[at(2, uint16_t(p + r * 12 + c))];
                        if (r == 0 || r == 7 || c == 0 || c == 11) border[arena_chr(i)].insert(t);
                        else if (t != BLANK) font[arena_chr(i)].insert(t);
                    }
                if (prg[at(2, uint16_t(p + 0x60))]) break;
            }
        }
        for (auto& [bank, set] : font) {
            std::vector<uint8_t>& v = L.bubble_codes[bank];
            for (uint8_t c : set) if (!border[bank].count(c)) v.push_back(c);
            // the US release's arenas all have its whole bubble font (A-Z . , · ! ')
            if (nes.game == Game::US)
                for (int c = 0xD7; c <= 0xFE; c++) if ((c <= 0xF0 || c >= 0xFA) && !set.count(uint8_t(c))) v.push_back(uint8_t(c));
            const uint8_t* t = nes.chr.data() + bank * 0x1000;
            for (int c = 0xC0; c < 0x100; c++)
                if (!set.count(uint8_t(c)) && !border[bank].count(uint8_t(c)) && uniform(t + c * 16)) L.bubble_spare[bank].push_back(uint8_t(c));
        }

        report("\n---- speech bubbles (10 x 6 tiles inside, placed like the US release's; Japanese: 3 lines, the marks in the row above each)\n");
        for (int i = 0; i < BUBBLES; i++) {
            uint16_t p = uint16_t(prg[at(2, uint16_t(A.bubble_lo + i))] | prg[at(2, uint16_t(A.bubble_hi + i))] << 8);
            int pages = 0;
            if (p >> 8)
                for (uint16_t q = p; pages < 8; q = uint16_t(q + 0x61)) { pages++; if (prg[at(2, uint16_t(q + 0x60))]) break; }
            for (int page = 0; page < 8; page++) {
                char id[24];
                if (page == 0) std::snprintf(id, sizeof id, "bubble.%02X", i);
                else std::snprintf(id, sizeof id, "bubble.%02X.%d", i, page + 1);
                const char* t = text(id);
                const bool have = t && *t && std::string(t).find_first_not_of(" \n") != std::string::npos;
                if (page >= pages) {
                    if (have) problem(id, "this game has no such bubble page, so it isn't shown");
                    continue;
                }
                if (!have) { problem(id, "no text: the game's own bubble is shown"); continue; }
                std::vector<Line> ls = lay(t, 10, id);   // (the CSV's lines as typed; too long ones are word-wrapped)
                const int max_lines = ja ? 3 : 6;
                if (int(ls.size()) > max_lines) { problem(id, std::to_string(ls.size()) + " lines, " + std::to_string(max_lines) + " fit"); ls.resize(max_lines); }
                for (auto& l : ls) if (l.size() > 10) l.resize(10);
                // Placed like the US release's own bubbles (inside rows 0-5; row 0 is left empty):
                // 1 line on row 2, centred; 2 lines on rows 1 and 3; 3-5 lines from row 1 down (6 use
                // them all). Japanese keeps a row above each line for its marks: rows 2 / 1, 3 / 1, 3, 5.
                // Lines start one tile in from the left edge, or at the edge if one of them is 10 wide.
                std::vector<int> rows;
                const int n = int(ls.size());
                // (built and moved in: assigning a brace list to the empty vector sets off a false GCC 15 -Wnonnull warning)
                if (n == 1) rows = std::vector<int>{2};
                else if (n == 2) rows = std::vector<int>{1, 3};
                else if (ja) rows = std::vector<int>{1, 3, 5};
                else for (int k = 0; k < n; k++) rows.push_back(n == 6 ? k : k + 1);
                int w = 0;
                for (auto& l : ls) w = std::max(w, int(l.size()));
                std::vector<int> xs;
                for (auto& l : ls) xs.push_back(n == 1 ? (10 - int(l.size())) / 2 : w == 10 ? 0 : 1);
                std::array<int16_t, 60> cells_;
                cells_.fill(-1);
                bool marks_lost = false;
                for (int k = 0; k < n; k++)
                    for (size_t c = 0; c < ls[k].size(); c++) {
                        const int x = xs[k] + int(c);
                        cells_[rows[k] * 10 + x] = int16_t(ls[k][c].glyph);
                        if (ls[k][c].mark >= 0) {
                            if (rows[k] > 0 && cells_[(rows[k] - 1) * 10 + x] < 0) cells_[(rows[k] - 1) * 10 + x] = int16_t(ls[k][c].mark);
                            else marks_lost = true;
                        }
                    }
                if (marks_lost) problem(id, "no room above for some ゛/゜");
                L.bubbles[i * 8 + page] = cells_;
                // will its arena's bubble font have enough tiles?
                const int bank = arena_chr(i);
                TileOverride scratch;
                Alloc a(nes.chr.data() + bank * 0x1000, &scratch, Alloc::IN_USE);
                for (uint8_t c : L.bubble_codes[bank]) a.make_free(c);
                std::vector<int> need(cells_.begin(), cells_.end());
                a.assign(need, L.glyphs);
                if (a.full) problem(id, "too many different characters for this arena's bubble font");
                // next to it, the US release's own bubble (its English text, as it lays it out)
                const std::vector<std::string> us = us_bubble(i, page);
                const std::string us_edge = us.empty() ? "" : "      +----------+";
                report(std::string(id) + (us.empty() ? "   (no bubble here in the US release)" : "   (right: the US release's own)") +
                       "\n  +----------+" + us_edge + "\n");
                for (int r = 0; r < 6; r++) {
                    std::string s = "  |";
                    for (int c = 0; c < 10; c++) {
                        const int g = cells_[r * 10 + c];
                        s += g < 0 ? std::string(ja ? "　" : " ") : utf8(glyph_char[g]);
                    }
                    s += "|";
                    if (!us.empty()) s += "      |" + us[r] + "|";
                    report(s + "\n");
                }
                report("  +----------+" + us_edge + "\n");
                if (ja) {   // the grid shows ゛/゜ as the game draws them, in the row above; this is how it reads
                    std::string reads;
                    for (auto& l : ls) reads += (reads.empty() ? "" : " / ") + show(l);
                    report("  reads: " + reads + "\n");
                }
            }
        }
    }
};

}  // namespace

bool read_rom_file(const std::string& path, Game g, std::vector<uint8_t>* prg, std::vector<uint8_t>* chr, std::string& err);

bool apply_language(Nes& nes, Language l, const std::string& other_rom, std::string& err, std::string* report) {
    nes.lang = LangRuntime{};
    const bool proto = nes.game == Game::Proto;
    if (!language_changes(l, proto)) return true;
    std::vector<uint8_t> other_chr;
    std::vector<uint8_t> other_prg;   // (the US release's, for the report)
    if (other_rom.empty() || !read_rom_file(other_rom, proto ? Game::US : Game::Proto, proto && report ? &other_prg : nullptr, &other_chr, err)) {
        if (other_rom.empty())
            err = proto ? "English needs the US release's ROM (for its font)."
                        : "日本語 (Japanese) needs the Japanese prototype's ROM (for its font): put \"Parody World - Monster Party (Japan) (Proto).nes\" in the roms folder.";
        return false;
    }
    LangRuntime& L = nes.lang;
    L.glyphs.reserve(512);   // the overrides point into it: never reallocated
    Setup s{nes, L, proto ? JP_ADDRS : US_ADDRS, !proto, {}, nullptr, {}, {}, {}, "", report != nullptr};
    s.chr_of[US_ROM] = proto ? &other_chr : &nes.chr;
    s.chr_of[JP_ROM] = proto ? &nes.chr : &other_chr;
    if (report) s.us_prg = proto ? &other_prg : &nes.prg;   // (bubbles are read before anything there is edited)
    s.report(proto ? "==== Parody World (prototype), English\n" : "==== Monster Party (US release), 日本語\n");
    s.story();
    s.screens();
    s.bubbles();
    if (L.glyphs.size() > 512) { err = "language: too many tiles"; return false; }
    if (report) {
        *report += s.rep;
        *report += "\n---- problems (" + std::to_string(s.problems.size()) + ")\n";
        for (auto& p : s.problems) *report += "  " + p + "\n";
        *report += "\n" + std::to_string(L.glyphs.size()) + " different characters\n";
    }
    L.japanese = !proto;
    L.active = true;
    return true;
}

// ---------------------------------------------------------------- speech bubbles, as the game shows them
// (translate.py LANG_HOOKS, both versions)
void Cpu::lang_bubble_start() {
    nes->lang.bubble_index = A;
    nes->lang.bubble_page = 0;
}

void Cpu::lang_bubble_page() {
    LangRuntime& L = nes->lang;
    if (!L.active) return;
    const int key = L.bubble_index * 8 + L.bubble_page++;
    auto it = L.bubbles.find(key);
    if (it == L.bubbles.end()) { L.bubble_chr = -1; return; }   // no text: the game's own bubble
    const int bank = ram[PlayfieldChr1Bank] & 0x1F;
    L.bubble = TileOverride{};
    Alloc a(nes->chr.data() + bank * 0x1000, &L.bubble, Alloc::IN_USE);
    for (uint8_t c : L.bubble_codes[bank]) a.make_free(c);
    for (uint8_t c : L.bubble_spare[bank]) a.make_free(c);   // (another round's bubble here, with Remix)
    a.state[BLANK] = Alloc::NEVER;
    std::vector<int> need(it->second.begin(), it->second.end());
    a.assign(need, L.glyphs);
    if (a.full) std::fprintf(stderr, "language: bubble %02X page %d: not enough font tiles in CHR bank $%02X\n", L.bubble_index, L.bubble_page, bank);
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 10; c++) {
            const int g = it->second[r * 10 + c];
            const int code = g < 0 ? BLANK : a.code(g);
            ram[0x55C + (r + 1) * 12 + c + 1] = uint8_t(code < 0 ? BLANK : code);
        }
    L.bubble_chr = bank;
}

std::string lang_tile_text(const Nes& nes, const TileOverride* ov, int bank, uint8_t code) {
    if (code == BLANK) return " ";
    const uint8_t* t = ov && ov->t[code] ? ov->t[code] : nes.chr.data() + bank * 0x1000 + code * 16;
    const LangRuntime& L = nes.lang;
    for (size_t i = 0; i < L.glyphs.size(); i++)
        if (std::memcmp(L.glyphs[i].data(), t, 16) == 0) return L.glyph_chars[i];
    return "?";
}
