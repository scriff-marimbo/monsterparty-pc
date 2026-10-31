#include "palette.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------- built in (generated from the NTSC signal levels)
void default_palette(uint32_t out[64]) {
    const float levels[8] = {.350f, .518f, .962f, 1.550f, 1.094f, 1.506f, 1.962f, 1.962f};
    const float black = .518f, white = 1.962f;
    for (int c = 0; c < 64; c++) {
        int hue = c & 15, lvl = (c >> 4) & 3;
        if (hue >= 0xE) { out[c] = 0xFF000000; continue; }
        float lo = levels[lvl + 4 * (hue == 0)], hi = levels[lvl + 4 * (hue < 0xD)];
        float y = 0, i = 0, q = 0;
        for (int p = 0; p < 12; p++) {
            // colour phase: the chroma wave for hue h is high for 6 of the 12 subcarrier phases, offset by 8
            float spot = ((hue + p + 8) % 12 < 6) ? hi : lo;
            spot = (spot - black) / (white - black);
            y += spot; i += spot * std::cos(3.14159265f * p / 6); q += spot * std::sin(3.14159265f * p / 6);
        }
        // saturation 1.5 matches the commonly used 2C02 reference colours ($16 red, $12 blue, $2A green ...)
        y /= 12; i = i / 12 * 1.5f; q = q / 12 * 1.5f;
        auto g = [](float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return int(v * 255 + 0.5f); };
        int r = g(y + 0.946882f * i + 0.623557f * q), gg = g(y - 0.274788f * i - 0.635691f * q), b = g(y - 1.108545f * i + 1.709007f * q);
        out[c] = 0xFF000000u | r << 16 | gg << 8 | b;
    }
}

// ---------------------------------------------------------------- files
static bool read_all(const std::string& path, std::string& out) {
    FILE* f = mp_fopen(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && out.size() < (1 << 20)) out.append(buf, n);
    std::fclose(f);
    return true;
}

static std::string stem(const std::string& path) {
    size_t s = path.find_last_of('/');
    std::string f = s == std::string::npos ? path : path.substr(s + 1);
    size_t dot = f.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? f : f.substr(0, dot);
}

static bool ends_with(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}

// JASC-PAL (Paint Shop Pro, text): "JASC-PAL", "0100", the colour count, then one "R G B" line per
// colour (0-255; a 4th alpha value is ignored). The NES colours $00-$3F are the first 64 entries.
static bool parse_jasc(const std::string& text, uint32_t out[64], std::string& err) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        line.erase(line.find_last_not_of(" \t\r") + 1);
        line.erase(0, line.find_first_not_of(" \t"));
        lines.push_back(line);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (lines.size() < 3 || lines[0] != "JASC-PAL") { err = "not a JASC-PAL file"; return false; }
    int count = std::atoi(lines[2].c_str());
    if (count < 64) {
        err = "has " + std::to_string(count) + " colours; it needs the 64 NES colours, $00 to $3F in order";
        return false;
    }
    for (int c = 0; c < 64; c++) {
        size_t l = 3 + size_t(c);
        int r, g, b;
        if (l >= lines.size() || std::sscanf(lines[l].c_str(), "%d %d %d", &r, &g, &b) != 3 ||
            r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            char m[96];
            std::snprintf(m, sizeof m, "line %zu (colour $%02X): expected \"R G B\" with values 0-255", l + 1, c);
            err = m;
            return false;
        }
        out[c] = 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
    }
    return true;
}

bool load_palette(const std::string& path, uint32_t out[64], std::string* name, std::string& err) {
    if (!ends_with(path, ".pal")) { err = path + ": palette files must be .pal (JASC-PAL text, or the 192-byte binary emulator format)"; return false; }
    std::string data;
    if (!read_all(path, data)) { err = "cannot read " + path; return false; }
    if (name) *name = stem(path);
    uint32_t tmp[64];
    if (data.compare(0, 8, "JASC-PAL") == 0) {
        if (!parse_jasc(data, tmp, err)) { err = path + ": " + err; return false; }
    } else {
        if (data.size() < 192) { err = path + ": not a JASC-PAL file, and too short for a binary palette (64 RGB colours, 192 bytes)"; return false; }
        for (int c = 0; c < 64; c++)
            tmp[c] = 0xFF000000u | uint8_t(data[c * 3]) << 16 | uint8_t(data[c * 3 + 1]) << 8 | uint8_t(data[c * 3 + 2]);
    }
    std::copy(tmp, tmp + 64, out);
    return true;
}

bool save_palette(const std::string& path, const uint32_t rgb[64], std::string& err) {
    FILE* f = mp_fopen(path, "wb");
    if (!f) { err = "cannot write " + path; return false; }
    std::fprintf(f, "JASC-PAL\n0100\n64\n");
    for (int c = 0; c < 64; c++)
        std::fprintf(f, "%u %u %u\n", unsigned(rgb[c] >> 16 & 0xFF), unsigned(rgb[c] >> 8 & 0xFF), unsigned(rgb[c] & 0xFF));
    bool ok = std::fclose(f) == 0;
    if (!ok) err = "cannot write " + path;
    return ok;
}

// ---------------------------------------------------------------- folders
static bool dir_exists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string user_palette_dir() {
    return mp_config_dir() + "palettes/";
}

std::string bundled_palette_dir() {
    if (const char* base = SDL_GetBasePath()) {
        std::string d = std::string(base) + "palettes/";
        if (dir_exists(d)) return d;
    }
#ifdef MP_PALETTE_DIR
    if (dir_exists(MP_PALETTE_DIR)) return std::string(MP_PALETTE_DIR) + "/";
#endif
    return "";
}

// letters and digits only, lower case: "warm-crt" and "Warm CRT" both become "warmcrt"
static std::string loose(const std::string& s) {
    std::string o;
    for (char c : s) if (std::isalnum(static_cast<unsigned char>(c))) o += char(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

std::string resolve_palette_path(const std::string& setting) {
    if (setting.empty() || mp_is_absolute(setting)) return setting;
    return bundled_palette_dir() + setting;
}

std::string upgrade_palette_setting(const std::string& setting) {
    if (setting.empty() || mp_is_absolute(setting) || !ends_with(setting, ".txt")) return setting;
    for (auto& p : list_palettes())
        if (!p.user && loose(stem(p.setting)) == loose(stem(setting))) return p.setting;
    return setting;
}

static void scan(const std::string& dir, bool user, std::vector<PaletteFile>& out) {
    if (dir.empty() || !dir_exists(dir)) return;
    int n = 0;
    char** names = SDL_GlobDirectory(dir.c_str(), nullptr, 0, &n);
    std::vector<PaletteFile> found;
    for (int i = 0; names && i < n; i++) {
        std::string file = names[i];
        if (!ends_with(file, ".pal")) continue;
        std::string path = dir + file, name, err;
        uint32_t tmp[64];
        if (!load_palette(path, tmp, &name, err)) continue;   // not a palette (or broken): leave it out
        found.push_back({name, user ? path : file, user});
    }
    SDL_free(names);
    std::sort(found.begin(), found.end(), [](const PaletteFile& a, const PaletteFile& b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
}

std::vector<PaletteFile> list_palettes() {
    std::vector<PaletteFile> out;
    scan(bundled_palette_dir(), false, out);
    scan(user_palette_dir(), true, out);
    return out;
}
