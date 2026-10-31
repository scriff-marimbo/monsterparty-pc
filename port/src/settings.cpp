#include "settings.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string config_dir() { return mp_config_dir(); }

std::string settings_path() { return config_dir() + "settings.ini"; }
std::string leaderboards_path() { return config_dir() + "leaderboards.ini"; }

// ---------------------------------------------------------------- controls
InputMap default_input_map() {
    InputMap m;
    for (int a = 0; a < IN_COUNT; a++)
        { m.keys[a] = 0; m.pad[a] = PAD_NONE; }
    m.keys[IN_UP] = SDL_SCANCODE_UP;       m.pad[IN_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP;
    m.keys[IN_DOWN] = SDL_SCANCODE_DOWN;   m.pad[IN_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    m.keys[IN_LEFT] = SDL_SCANCODE_LEFT;   m.pad[IN_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    m.keys[IN_RIGHT] = SDL_SCANCODE_RIGHT; m.pad[IN_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    m.keys[IN_A] = SDL_SCANCODE_X;         m.pad[IN_A] = SDL_GAMEPAD_BUTTON_EAST;        // B (right) on an Xbox pad
    m.keys[IN_B] = SDL_SCANCODE_Z;         m.pad[IN_B] = SDL_GAMEPAD_BUTTON_SOUTH;       // A (bottom)
    m.keys[IN_TURBO_B] = SDL_SCANCODE_A;   m.pad[IN_TURBO_B] = SDL_GAMEPAD_BUTTON_WEST;  // X (left)
    m.keys[IN_SELECT] = SDL_SCANCODE_RSHIFT; m.pad[IN_SELECT] = SDL_GAMEPAD_BUTTON_BACK;
    m.keys[IN_START] = SDL_SCANCODE_RETURN;  m.pad[IN_START] = SDL_GAMEPAD_BUTTON_START;
    m.left_stick = true;
    return m;
}

static const char* const kActionKeys[IN_COUNT] = {"up", "down", "left", "right", "a", "b", "turbo_b", "select", "start"};

const char* input_action_name(int a) {
    static const char* const names[IN_COUNT] = {"Up", "Down", "Left", "Right", "A", "B", "Turbo B", "Select", "Start"};
    return a >= 0 && a < IN_COUNT ? names[a] : "?";
}

std::string key_display_name(int sc) {
    if (sc <= 0) return "-";
    const char* n = SDL_GetScancodeName(SDL_Scancode(sc));
    return n && *n ? n : "Key " + std::to_string(sc);
}

std::string pad_display_name(int code) {
    switch (code) {
    case PAD_NONE: return "-";
    case PAD_LEFT_TRIGGER: return "Left trigger";
    case PAD_RIGHT_TRIGGER: return "Right trigger";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A (bottom)";
    case SDL_GAMEPAD_BUTTON_EAST: return "B (right)";
    case SDL_GAMEPAD_BUTTON_WEST: return "X (left)";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y (top)";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Left stick click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Right stick click";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "Left bumper";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "Right bumper";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
    }
    const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(code));
    return n ? n : "Button " + std::to_string(code);
}

// settings.ini spellings: SDL's own names, so they stay readable and stable
static std::string pad_code_string(int code) {
    if (code == PAD_NONE) return "";
    if (code == PAD_LEFT_TRIGGER) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    if (code == PAD_RIGHT_TRIGGER) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(code));
    return n ? n : "";
}

static int pad_code_from_string(const std::string& v) {
    if (v.empty()) return PAD_NONE;
    SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(v.c_str());
    if (ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return PAD_LEFT_TRIGGER;
    if (ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return PAD_RIGHT_TRIGGER;
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(v.c_str());
    return b == SDL_GAMEPAD_BUTTON_INVALID ? PAD_NONE : int(b);
}

// "key.turbo_b=A" / "pad.b=a" ; returns false if the key isn't one of these
static bool load_input_line(InputMap& m, const std::string& key, const std::string& val) {
    if (key == "pad.left_stick") { m.left_stick = val == "1"; return true; }
    size_t dot = key.find('.');
    if (dot == std::string::npos) return false;
    std::string kind = key.substr(0, dot), act = key.substr(dot + 1);
    int a = -1;
    for (int i = 0; i < IN_COUNT; i++) if (act == kActionKeys[i]) a = i;
    if (a < 0) return false;
    if (kind == "key") { m.keys[a] = val.empty() ? 0 : int(SDL_GetScancodeFromName(val.c_str())); return true; }
    if (kind == "pad") { m.pad[a] = pad_code_from_string(val); return true; }
    if (kind == "key2" || kind == "pad2") return true;   // second keys/buttons from an earlier version: ignored
    return false;
}

static void save_input(FILE* f, const InputMap& m) {
    std::fprintf(f, "# controls: SDL key names (key) and SDL gamepad button names (pad); empty = none\n");
    std::fprintf(f, "# gamepad buttons by position: a = bottom, b = right, x = left, y = top\n");
    for (int a = 0; a < IN_COUNT; a++) {
        const char* name = m.keys[a] > 0 ? SDL_GetScancodeName(SDL_Scancode(m.keys[a])) : "";
        std::fprintf(f, "key.%s=%s\n", kActionKeys[a], name ? name : "");
        std::fprintf(f, "pad.%s=%s\n", kActionKeys[a], pad_code_string(m.pad[a]).c_str());
    }
    std::fprintf(f, "pad.left_stick=%d\n", m.left_stick ? 1 : 0);
}

static const char* mode_name(DisplayMode m) {
    return m == DisplayMode::Fullscreen ? "fullscreen" : m == DisplayMode::FullscreenExclusive ? "exclusive" : "windowed";
}

void load_settings(Settings& s) {
    FILE* f = mp_fopen(settings_path(), "r");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        line[std::strcspn(line, "\r\n")] = 0;
        char* eq = std::strchr(line, '=');
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq = 0;
        std::string key = line, val = eq + 1;
        int n = std::atoi(val.c_str());
        if (key == "mode") s.mode = val == "fullscreen" ? DisplayMode::Fullscreen
                                  : val == "exclusive" ? DisplayMode::FullscreenExclusive : DisplayMode::Windowed;
        else if (key == "display") s.display = val;
        else if (key == "window_scale" && n >= 1 && n <= 16) s.window_scale = n;
        else if (key == "fullscreen_scale" && n >= 0 && n <= 16) s.fullscreen_scale = n;
        else if (key == "exclusive_width" && n >= 0) s.exclusive_w = n;
        else if (key == "exclusive_height" && n >= 0) s.exclusive_h = n;
        else if (key == "exclusive_hz") s.exclusive_hz = float(std::atof(val.c_str()));
        else if (key == "aspect") s.aspect_8_7 = val == "8:7";
        else if (key == "widescreen") s.widescreen = n != 0;
        else if (key == "mode3d") s.mode3d = n != 0;
        else if (key == "fill") s.fill_screen = n != 0;
        else if (key == "shader") s.shader = val;
        else if (key == "palette") s.palette = val;
        else if (key == "show_launcher") s.show_launcher = n != 0;
        else if (key == "prototype") s.prototype = n != 0;
        else if (key == "language") s.language = val == "en" || val == "ja" ? val : "default";
        else if (key == "sound") s.sound = n != 0;
        else if (key == "leaderboards") s.leaderboards = n != 0;
        else if (key == "remix") s.remix = n != 0;
        else if (key == "totem_pole" || key == "remix_totem_pole") s.totem_pole = n != 0;   // remix_totem_pole: older files
        else if (key == "fix_round7_key") s.fix_round7_key = n != 0;
        else if (key == "totem_finished" || key == "totem_attack") s.totem_attack = n != 0;   // totem_attack: older files
        else if (key == "remix_seed") s.remix_seed = val;
        else if (key == "remix_last_seed") s.remix_last_seed = val;
        else if (key == "trainer.infinite_life") s.trainer_infinite_life = n != 0;
        else if (key == "trainer.full_life") s.trainer_full_life = n != 0;
        else if (key == "trainer.start_round") { s.trainer_start_round_on = n != 0; if (n >= 2 && n <= 8) s.trainer_start_round = n; }
        else if (key == "trainer.start_round_number" && n >= 2 && n <= 8) s.trainer_start_round = n;
        else if (key == "trainer.super_jump") s.trainer_super_jump = n != 0;
        else if (key == "trainer.moon_jump") s.trainer_moon_jump = n != 0;
        else if (key == "trainer.always_bert") s.trainer_always_bert = n != 0;
        else if (key == "trainer.mark_doors") s.trainer_mark_doors = n != 0;
        else if (key == "music") { s.music_midi = val == "midi"; s.music_off = val == "off"; }
        else if (key == "soundfont") s.soundfont = val;
        else if (key == "midi.pulse1" && n >= 0 && n < 128) s.midi_program[0] = n;
        else if (key == "midi.pulse2" && n >= 0 && n < 128) s.midi_program[1] = n;
        else if (key == "midi.triangle" && n >= 0 && n < 128) s.midi_program[2] = n;
        else if (key == "difficulty") s.difficulty = val == "casual" ? 0 : val == "nasty" ? 2 : 1;
        else load_input_line(s.input, key, val);
    }
    std::fclose(f);
}

bool save_settings(const Settings& s) {
    std::string path = settings_path();
    std::string dir = path.substr(0, path.find_last_of('/'));
    SDL_CreateDirectory(dir.c_str());   // makes parent folders too; fine if it already exists
    FILE* f = mp_fopen(path, "w");
    if (!f) return false;
    std::fprintf(f, "# Monster Party port settings (written by the launcher)\n");
    std::fprintf(f, "mode=%s\n", mode_name(s.mode));
    std::fprintf(f, "display=%s\n", s.display.c_str());
    std::fprintf(f, "window_scale=%d\n", s.window_scale);
    std::fprintf(f, "fullscreen_scale=%d\n", s.fullscreen_scale);
    std::fprintf(f, "exclusive_width=%d\nexclusive_height=%d\nexclusive_hz=%.2f\n", s.exclusive_w, s.exclusive_h, s.exclusive_hz);
    std::fprintf(f, "aspect=%s\n", s.aspect_8_7 ? "8:7" : "1:1");
    std::fprintf(f, "widescreen=%d\n", s.widescreen ? 1 : 0);
    std::fprintf(f, "mode3d=%d\n", s.mode3d ? 1 : 0);
    std::fprintf(f, "fill=%d\n", s.fill_screen ? 1 : 0);
    std::fprintf(f, "palette=%s\n", s.palette.c_str());
    std::fprintf(f, "shader=%s\n", s.shader.c_str());
    std::fprintf(f, "show_launcher=%d\n", s.show_launcher ? 1 : 0);
    std::fprintf(f, "prototype=%d\n", s.prototype ? 1 : 0);
    std::fprintf(f, "# language: default (each game's own text), en (English) or ja (Japanese; needs the prototype's ROM)\n");
    std::fprintf(f, "language=%s\n", s.language.c_str());
    std::fprintf(f, "leaderboards=%d\n", s.leaderboards ? 1 : 0);
    std::fprintf(f, "remix=%d\n", s.remix ? 1 : 0);
    std::fprintf(f, "# empty = a new random remix each time\n");
    std::fprintf(f, "remix_seed=%s\n", s.remix_seed.c_str());
    std::fprintf(f, "remix_last_seed=%s\n", s.remix_last_seed.c_str());
    std::fprintf(f, "# Bosses: totem_pole (prototype) = the unused Totem Pole boss; fix_round7_key = round 7 needs all 3 bosses\n");
    std::fprintf(f, "totem_pole=%d\n", s.totem_pole ? 1 : 0);
    std::fprintf(f, "totem_finished=%d\n", s.totem_attack ? 1 : 0);
    std::fprintf(f, "fix_round7_key=%d\n", s.fix_round7_key ? 1 : 0);
    std::fprintf(f, "# Trainers (cheats): 1 = on. trainer.start_round: 0 = off, else the round (2-8)\n");
    std::fprintf(f, "trainer.infinite_life=%d\n", s.trainer_infinite_life ? 1 : 0);
    std::fprintf(f, "trainer.full_life=%d\n", s.trainer_full_life ? 1 : 0);
    std::fprintf(f, "trainer.start_round=%d\n", s.trainer_start_round_on ? s.trainer_start_round : 0);
    std::fprintf(f, "trainer.start_round_number=%d\n", s.trainer_start_round);
    std::fprintf(f, "trainer.super_jump=%d\n", s.trainer_super_jump ? 1 : 0);
    std::fprintf(f, "trainer.moon_jump=%d\n", s.trainer_moon_jump ? 1 : 0);
    std::fprintf(f, "trainer.always_bert=%d\n", s.trainer_always_bert ? 1 : 0);
    std::fprintf(f, "trainer.mark_doors=%d\n", s.trainer_mark_doors ? 1 : 0);
    std::fprintf(f, "sound=%d\n", s.sound ? 1 : 0);
    std::fprintf(f, "music=%s\n", s.music_off ? "off" : s.music_midi ? "midi" : "nes");
    std::fprintf(f, "soundfont=%s\n", s.soundfont.c_str());
    std::fprintf(f, "# General MIDI program numbers (0-127) for the MIDI music's channels\n");
    std::fprintf(f, "midi.pulse1=%d\nmidi.pulse2=%d\nmidi.triangle=%d\n", s.midi_program[0], s.midi_program[1], s.midi_program[2]);
    std::fprintf(f, "difficulty=%s\n", s.difficulty == 0 ? "casual" : s.difficulty == 2 ? "nasty" : "normal");
    save_input(f, s.input);
    std::fclose(f);
    return true;
}
