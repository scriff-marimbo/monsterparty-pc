// Monster Party native port: the NES colour palette (64 colours) and palette files.
//
// The built-in palette is generated from the NTSC signal levels. Others are read from .pal files:
//   - JASC-PAL (text, the Paint Shop Pro format most image editors read and write): "JASC-PAL",
//     "0100", the colour count, then one "R G B" line per colour (0-255). The first 64 entries are
//     the NES colours $00-$3F in order. The palette's name is the file name.
//   - binary: the usual emulator format, 64 RGB triples (192 bytes). Longer files (with the
//     emphasis variants, 1536 bytes) work too; only the first 64 colours are used.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The built-in palette (ARGB, alpha FF).
void default_palette(uint32_t out[64]);

// Reads a .pal file. name gets the file name without ".pal". On failure err says why.
bool load_palette(const std::string& path, uint32_t out[64], std::string* name, std::string& err);

// Writes a JASC-PAL file with the 64 colours.
bool save_palette(const std::string& path, const uint32_t rgb[64], std::string& err);

struct PaletteFile {
    std::string name;      // shown in the settings window
    std::string setting;   // what settings.ini stores: bundled file name, or the full path of the player's own
    bool user;             // from the player's own folder
};

std::string user_palette_dir();      // palettes/ in mp_config_dir(): ~/.config/monsterparty/palettes/ (ends in a separator)
std::string bundled_palette_dir();   // "palettes/" next to the executable, else the source folder's; "" if none
std::vector<PaletteFile> list_palettes();               // bundled ones, then the player's, each sorted by name
std::string resolve_palette_path(const std::string& setting);   // a bare file name is a bundled palette
std::string upgrade_palette_setting(const std::string& setting); // an old bundled ".txt" setting -> the .pal of the same name
