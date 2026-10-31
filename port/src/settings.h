// Monster Party native port: settings (video, palette, shader, controls), chosen in the launcher and saved between runs.
#pragma once
#include <string>

enum class DisplayMode { Windowed, Fullscreen, FullscreenExclusive };

// ---- controls: what each NES button (and turbo B) is on the keyboard and the gamepad
enum InputAction { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_A, IN_B, IN_TURBO_B, IN_SELECT, IN_START, IN_COUNT };
constexpr int PAD_NONE = -1;
constexpr int PAD_LEFT_TRIGGER = 100;     // gamepad codes: SDL_GamepadButton values, plus these two triggers
constexpr int PAD_RIGHT_TRIGGER = 101;

struct InputMap {
    int keys[IN_COUNT];                   // one key per action: SDL_Scancode, 0 = none
    int pad[IN_COUNT];                    // one gamepad button per action: SDL_GamepadButton or PAD_*_TRIGGER, PAD_NONE = none
    bool left_stick = true;               // the left stick also works as the d-pad
};
InputMap default_input_map();
const char* input_action_name(int action);   // "Up", ..., "Turbo B"
std::string key_display_name(int scancode);  // "Z", "Right Shift", "-" for none
std::string pad_display_name(int code);      // "A (bottom)", "D-pad up", "-" for none

struct Settings {
    DisplayMode mode = DisplayMode::Windowed;
    std::string display;          // monitor name; empty or not found = the primary monitor
    int window_scale = 3;         // windowed: starting window size in NES pixels x this
    int fullscreen_scale = 0;     // fullscreen: 0 = largest whole number that fits
    int exclusive_w = 0, exclusive_h = 0;   // exclusive fullscreen video mode; 0 = the desktop's
    float exclusive_hz = 0;
    bool aspect_8_7 = true;       // true: NES TV pixel shape (8:7, horizontal not whole-number); false: square pixels
    bool fill_screen = false;     // scale to the full height (or width) of the window/screen, not a whole number
    bool widescreen = false;      // 16:9: show more of the room on each side (experimental)
    bool mode3d = false;          // 3D mode (drawing only; OpenGL, no shader; see render3d.h)
    std::string palette;          // empty = built in; a bundled palette's file name, or the full path of the player's own
    std::string shader;           // empty = none; a .glslp/.glsl path, relative to shader_root() for bundled ones
    bool sound = true;            // false: no audio output at all (the audio device isn't opened)
    bool music_off = false;       // no music at all (sound effects still play); music_midi is then ignored
    bool music_midi = false;      // music through a SoundFont (MIDI) instead of the NES sound chip; sound effects stay on the chip
    std::string soundfont;        // empty = the first bundled one; a bundled file name, or the full path of the player's own
    int midi_program[3] = {80, 80, 33};   // General MIDI programs (0-based) for pulse 1, pulse 2, triangle
    InputMap input = default_input_map();
    int difficulty = 1;           // 0 Casual, 1 Normal (the game as released), 2 Nasty; see Difficulty in nes.h
    bool prototype = false;       // play the Japanese prototype (Parody World), if its ROM is in roms/
    std::string language = "default";   // "default" (each game's own text), "en" or "ja": see language.h
    bool remix = false;           // Remix Mode: bosses' doors and enemy types shuffled within each round (see remix.h)
    std::string remix_seed;       // empty = a new random remix each time; else a seed to replay ("1A2B-3C4D")
    std::string remix_last_seed;  // the last remix played (shown in the launcher so it can be replayed)
    // Bosses (with or without Remix; see apply_bosses in remix.h)
    bool totem_pole = false;      // prototype: the unused Totem Pole boss gets round 7's 3rd boss door
    bool totem_attack = true;     // ... 'finished': a 2nd animation frame, and he fires heads at the player (not in the game)
    bool fix_round7_key = false;  // round 7 needs all 3 bosses, so beating the 3rd can't take the key away
    // Trainers (cheats; see Trainers in nes.h)
    bool trainer_infinite_life = false;
    bool trainer_full_life = false;
    bool trainer_start_round_on = false;
    int trainer_start_round = 2;  // 2-8, used when trainer_start_round_on
    bool trainer_super_jump = false;
    bool trainer_moon_jump = false;   // Super Jump and Moon Jump: the launcher lets only one be on
    bool trainer_always_bert = false;
    bool trainer_mark_doors = false;  // Mark Round 6 Doors
    bool leaderboards = true;     // log finished games (no cheats) and offer the result screen after the credits; see leaderboard.h
    bool show_launcher = true;
};

// Widescreen adds this many NES pixels on each side, so the picture comes out about 16:9:
// 428 x 240 with square pixels, 374 x 240 with 8:7 ones.
inline int wide_margin(bool aspect_8_7) { return aspect_8_7 ? 59 : 86; }
inline int frame_width(bool aspect_8_7, bool wide) { return wide ? 256 + 2 * wide_margin(aspect_8_7) : 256; }

// Size of the picture (frame_width x 240 NES pixels) at a whole-number scale. With the 8:7 pixel
// shape the height stays a whole multiple and the width is rounded.
inline int picture_w(int scale, bool aspect_8_7, bool wide) {
    const int fw = frame_width(aspect_8_7, wide);
    return aspect_8_7 ? (fw * 8 * scale + 3) / 7 : fw * scale;
}
inline int picture_h(int scale) { return 240 * scale; }

std::string settings_path();      // settings.ini in mp_config_dir() (~/.config/monsterparty/, %APPDATA%\\NESdsm\\monsterparty\\)
std::string leaderboards_path();  // leaderboards.ini in the same folder
void load_settings(Settings& s);  // missing file or bad lines keep the defaults
bool save_settings(const Settings& s);
