// ROM loading and checking.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include "support.h"

using namespace mptest;

TEST_CASE("the ROMs are recognised by their CRC", "[rom]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        std::string err;
        CHECK(check_rom(rom_path(g), g, err));
        CHECK(err.empty());
        CHECK(load_prg(g).size() == 128 * 1024);
    }
}

TEST_CASE("a ROM is refused as the other version", "[rom]") {
    REQUIRE_ROM(Game::US);
    REQUIRE_ROM(Game::Proto);
    std::string err;
    CHECK_FALSE(check_rom(rom_path(Game::US), Game::Proto, err));
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK_FALSE(check_rom(rom_path(Game::Proto), Game::US, err));
}

TEST_CASE("missing and broken ROM files are refused", "[rom]") {
    std::string err;
    CHECK_FALSE(check_rom("/nonexistent/Monster Party (USA).nes", Game::US, err));
    CHECK_FALSE(err.empty());
    const std::string path = (std::filesystem::temp_directory_path() / "mp_test_not_a_rom.nes").string();
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fputs("NES\x1a not really a ROM", f); std::fclose(f); }
    err.clear();
    CHECK_FALSE(check_rom(path, Game::US, err));
    CHECK_FALSE(err.empty());
    std::remove(path.c_str());
}
