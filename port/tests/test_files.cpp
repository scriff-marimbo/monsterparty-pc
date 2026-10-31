// Settings, palettes and the port's own sprites.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "custom_sprites.h"
#include "palette.h"
#include "platform.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace {
std::string u8(const fs::path& p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// a fresh, empty config folder for the test ($MP_CONFIG_HOME, which mp_config_dir() reads first)
struct TempConfig {
    fs::path dir;
    TempConfig() {
        dir = fs::temp_directory_path() / ("mp_test_config_" + std::to_string(std::rand()));
        fs::create_directories(dir);
        set_env("MP_CONFIG_HOME", u8(dir));
    }
    ~TempConfig() { std::error_code ec; fs::remove_all(dir, ec); set_env("MP_CONFIG_HOME", ""); }
};
}  // namespace

TEST_CASE("settings are saved and read back", "[files]") {
    TempConfig tmp;
    Settings s;
    s.window_scale = 5;
    s.aspect_8_7 = false;
    s.widescreen = true;
    s.difficulty = 2;
    s.prototype = true;
    s.remix = true;
    s.remix_seed = "1A2B-3C4D";
    s.totem_pole = true;
    s.totem_attack = false;
    s.fix_round7_key = true;
    s.trainer_infinite_life = true;
    s.trainer_start_round_on = true;
    s.trainer_start_round = 6;
    s.trainer_moon_jump = true;
    s.trainer_always_bert = true;
    s.trainer_mark_doors = true;
    s.palette = "Vivid.pal";
    REQUIRE(save_settings(s));
    Settings t;
    load_settings(t);
    CHECK(t.window_scale == 5);
    CHECK_FALSE(t.aspect_8_7);
    CHECK(t.widescreen);
    CHECK(t.difficulty == 2);
    CHECK(t.prototype);
    CHECK(t.remix);
    CHECK(t.remix_seed == "1A2B-3C4D");
    CHECK(t.totem_pole);
    CHECK_FALSE(t.totem_attack);
    CHECK(t.fix_round7_key);
    CHECK(t.trainer_infinite_life);
    CHECK_FALSE(t.trainer_full_life);
    CHECK(t.trainer_start_round_on);
    CHECK(t.trainer_start_round == 6);
    CHECK(t.trainer_moon_jump);
    CHECK_FALSE(t.trainer_super_jump);
    CHECK(t.trainer_always_bert);
    CHECK(t.trainer_mark_doors);
    CHECK(t.palette == "Vivid.pal");
}

TEST_CASE("older settings names are still read", "[files]") {
    TempConfig tmp;
    fs::create_directories(tmp.dir / "monsterparty");
    FILE* f = mp_fopen(u8(tmp.dir / "monsterparty" / "settings.ini"), "w");
    REQUIRE(f);
    std::fputs("remix_totem_pole=1\ntotem_attack=0\nnonsense line\nwindow_scale=99\n", f);
    std::fclose(f);
    Settings t;
    load_settings(t);
    CHECK(t.totem_pole);
    CHECK_FALSE(t.totem_attack);
    CHECK(t.window_scale == Settings{}.window_scale);   // out of range: the default stays
}

TEST_CASE("a missing settings file gives the defaults", "[files]") {
    TempConfig tmp;
    Settings t;
    load_settings(t);
    CHECK(t.difficulty == 1);
    CHECK_FALSE(t.remix);
    CHECK_FALSE(t.trainer_infinite_life);
    CHECK(t.show_launcher);
}

TEST_CASE("palettes are written and read back", "[files]") {
    TempConfig tmp;
    uint32_t def[64], back[64];
    default_palette(def);
    CHECK(def[0x0F] == 0xFF000000u);           // black
    CHECK(def[0x30] != def[0x0F]);
    const std::string path = (tmp.dir / "test.pal").string();
    std::string err;
    REQUIRE(save_palette(path, def, err));
    REQUIRE(load_palette(path, back, nullptr, err));
    CHECK(std::memcmp(def, back, sizeof def) == 0);
}

TEST_CASE("a broken palette file is refused", "[files]") {
    TempConfig tmp;
    const std::string path = u8(tmp.dir / "bad.pal");
    FILE* f = mp_fopen(path, "w");
    std::fputs("JASC-PAL\n0100\n3\n1 2 3\n", f);
    std::fclose(f);
    uint32_t out[64];
    std::string err;
    CHECK_FALSE(load_palette(path, out, nullptr, err));
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK_FALSE(load_palette((tmp.dir / "missing.pal").string(), out, nullptr, err));
}

TEST_CASE("the bundled palettes all load", "[files]") {
    const auto all = list_palettes();
    CHECK_FALSE(all.empty());
    for (const auto& p : all) {
        uint32_t out[64];
        std::string err;
        INFO(p.setting);
        CHECK(load_palette(resolve_palette_path(p.setting), out, nullptr, err));
    }
}

TEST_CASE("the port's own sprites are well formed", "[files]") {
    using namespace custom_sprite;
    for (const Sprite* sp : {&TOTEM_HEAD, &TOTEM_FRAME2}) {
        for (int r = 0; r < sp->h; r++) {
            REQUIRE(std::strlen(sp->rows[r]) == size_t(sp->w));
            for (int c = 0; c < sp->w; c++) {
                const char p = sp->rows[r][c];
                if (p == '.') continue;
                CHECK(sp->colour(p) < 0x40);   // an NES colour
            }
        }
    }
    CHECK(TOTEM_HEAD.w == 16);
    CHECK(TOTEM_HEAD.h == 16);
    CHECK(TOTEM_FRAME2.h == 96);
}

// ---------------------------------------------------------------- platform.h

TEST_CASE("the config folder follows MP_CONFIG_HOME", "[files]") {
    TempConfig tmp;
    CHECK(mp_config_dir() == u8(tmp.dir) + "/monsterparty/");
    CHECK(settings_path() == u8(tmp.dir) + "/monsterparty/settings.ini");
    CHECK(user_palette_dir() == u8(tmp.dir) + "/monsterparty/palettes/");
}

TEST_CASE("files with non-ASCII names can be written, read, renamed and removed", "[files]") {
    TempConfig tmp;
    // "José" and "モンスター" in UTF-8: Windows' fopen can't open these, the mp_ helpers can
    const std::string dir = u8(tmp.dir) + "/Jos\xC3\xA9 \xE3\x83\xA2\xE3\x83\xB3\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC";
    fs::create_directories(fs::u8path(dir));
    const std::string a = dir + "/a.txt", b = dir + "/b.txt";
    for (const std::string* p : {&a, &b}) {
        FILE* f = mp_fopen(*p, "wb");
        REQUIRE(f);
        std::fputs(p == &a ? "first" : "second", f);
        std::fclose(f);
    }
    std::vector<unsigned char> d;
    REQUIRE(mp_read_file(a, d));
    CHECK(std::string(d.begin(), d.end()) == "first");
    CHECK(mp_rename(a, b));   // replaces b (std::rename wouldn't on Windows)
    REQUIRE(mp_read_file(b, d));
    CHECK(std::string(d.begin(), d.end()) == "first");
    CHECK_FALSE(mp_read_file(a, d));
    CHECK(mp_remove(b));
    CHECK_FALSE(mp_read_file(b, d));

    // a palette saved there and listed from the user's folder
    const std::string pal = dir + "/Mine.pal";
    uint32_t def[64], back[64];
    default_palette(def);
    std::string err;
    REQUIRE(save_palette(pal, def, err));
    REQUIRE(load_palette(pal, back, nullptr, err));
    CHECK(std::memcmp(def, back, sizeof def) == 0);
}

TEST_CASE("absolute paths and file names", "[files]") {
    CHECK(mp_is_absolute("/home/x/a.pal"));
    CHECK_FALSE(mp_is_absolute("a.pal"));
    CHECK_FALSE(mp_is_absolute("crt/crt-geom.glslp"));
    CHECK_FALSE(mp_is_absolute(""));
    CHECK(mp_file_name("/home/x/My palette.pal") == "My palette.pal");
    CHECK(mp_file_name("plain.sf2") == "plain.sf2");
#ifdef _WIN32
    CHECK(mp_is_absolute("C:\\Users\\x\\a.pal"));
    CHECK(mp_is_absolute("d:/games/a.pal"));
    CHECK(mp_is_absolute("\\\\server\\share\\a.pal"));
    CHECK_FALSE(mp_is_absolute("C:a.pal"));
    CHECK(mp_file_name("C:\\Users\\x\\My palette.pal") == "My palette.pal");
    CHECK(resolve_palette_path("C:\\Users\\x\\a.pal") == "C:\\Users\\x\\a.pal");
#endif
    CHECK(resolve_palette_path("/home/x/a.pal") == "/home/x/a.pal");
}
