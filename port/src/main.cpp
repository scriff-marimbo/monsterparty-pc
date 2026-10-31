// Monster Party native port: SDL3 front end.
//
// The ROMs are looked for in a "roms" folder: in the current folder, next to the executable, then
// the repository's roms/ (MP_ROM_DIR). "Monster Party (USA).nes" is required; with
// "Parody World - Monster Party (Japan) (Proto).nes" there too, the Japanese prototype can be played.
//
//   monsterparty                             play (window, keyboard / gamepad); a settings
//                                            window comes first unless it was switched off
//   monsterparty ROM.nes                     the same, with the US ROM at this path instead
//   monsterparty --prototype                 play the Japanese prototype (needs its ROM in roms/)
//   monsterparty --no-launcher               start straight away with the saved settings
//   monsterparty --headless N [--press FRAME:BUTTONS ...] [--dump]
//       run N frames without a window (for testing); BUTTONS = letters from "ABsSudlr"
//       (A, B, select, Start, up, down, left, right) held for 8 frames from FRAME;
//       --dump prints game state and the text on screen at the end;
//       --reset FRAME presses the console's Reset button at FRAME;
//       --wav FILE writes the game's sound to a 44.1 kHz mono WAV file;
//       --screenshot FILE.bmp saves the last frame.
//   --widescreen                             16:9 widescreen mode (experimental), headless or not
//   --3d                                     3D mode (experimental), headless or not; headless, --screenshot
//                                            then draws the last frame in 3D (OpenGL; SDL_VIDEODRIVER=offscreen works)
//   --camera YAW,PITCH,ZOOM                  headless --3d --screenshot: the camera (degrees, degrees, 1 = straight on)
//   --remix SEED                             Remix Mode with this seed (8 hex digits), headless or not
//   --totem-pole                             prototype: the unused Totem Pole gets round 7's 3rd boss door, headless or not
//   --no-totem-attack                        prototype: the Totem Pole as he is in the ROM (not 'finished')
//   --fix-round7                             round 7 needs all 3 bosses (fixes the key-steal bug), headless or not
//   --poke FRAME:ADDR=VALUE                  headless test: write a RAM byte (hex) at that frame
//   --boss-door BOSSID                       debug: every round's first boss door leads to this boss
//                                            (hex BossId $00-$1F, e.g. 15 = Chameleon Man), headless or not
//   --ending                                 debug: a new game goes straight to the ending story instead of the intro,
//                                            headless or not (counts as cheating: nothing is logged)
//   --enter-boss-door                        headless: start a game and go through round 1's first boss door
//   --dump-vram FILE                         headless: at the end, write the nametables (2 KB), the palette
//                                            (32 bytes) and the CHR banks / PPU control (3 bytes) to FILE
//   --trainers LIST                          trainers (cheats), headless or not: comma-separated from
//                                            life (Infinite Life), full (Start with Full Life), round=N
//                                            (Start on Round N, 2-8), super, moon (jumps), bert (Always Bert),
//                                            doors (Mark Round 6 Doors)
//   --leaderboards FILE                      headless: log a finished game to this leaderboards file (off in headless runs without it)
//   --no-leaderboards                        don't log finished games or show the result screen, whatever the setting
//   (in game: Tab toggles fast forward, 4x)
//   --palette FILE                           colours from a .pal palette file (JASC-PAL or binary), headless or not
//   --difficulty casual|normal|nasty         the port's difficulty setting (Normal = the original game), headless or not
//   --language default|en|ja                 the Language setting (en: the prototype in English, ja: the US
//                                            release in Japanese), headless or not
//   --lang-report                            print every translated text as it will be laid out, and what
//                                            doesn't fit (tools/text/text_catalogue.csv), then quit
//   --soundfont FILE.sf2                     play the music as MIDI through this SoundFont, headless or not
//   --midi FILE.mid                          headless: also record the music as a MIDI file
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // on Windows: the WinMain entry point, and argv in UTF-8
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "launcher.h"
#include "render3d.h"
#include "remix.h"
#include "nes.h"
#include "settings.h"
#include "gl_api.h"
#include "shader_chain.h"
#include "palette.h"
#include "music.h"
#include "custom_sprites.h"
#include "platform.h"
#include "../gen/ram_symbols.h"

static SDL_Window* window;
static SDL_Renderer* renderer;
// 3D mode: the scene the renderer fills, drawn with OpenGL (no SDL renderer, no shader)
static bool mode3d;
static Scene3D* scene3d;
static Renderer3D r3d;
static Camera3D cam3d;
static bool mouse_captured;   // 3D mode: the mouse is turning the camera (Esc lets it go, a click takes it back)
static void capture_mouse(bool on) {
    mouse_captured = on;
    SDL_SetWindowRelativeMouseMode(window, on);
}
static SDL_Texture* texture;
static SDL_Texture* sharp_texture;   // fill-screen mode: the whole-number upscale before the final smooth resize
static int sharp_w, sharp_h;
static SDL_GLContext gl_context;   // only when a shader is in use; then there's no SDL renderer
static ShaderChain shader;
static SDL_Gamepad* gamepad;
static SDL_AudioStream* audio_stream;
static uint64_t next_frame_ns;
static bool fast_forward;        // Tab: run at 4x (frames paced 4x faster, every 4th one shown and heard)
static constexpr int FAST_FORWARD = 4;
static std::string window_title; // the game window's normal title (fast forward adds to it)
static Settings settings;
static Nes* game_nes;   // for the window title
static std::string remix_text;   // the session's remix seed, for the window title (empty = no remix)
static const char* remix_arg;    // --remix SEED
static bool g_is_proto(const Nes& n) { return n.game == Game::Proto; }
static bool totem_pole_arg;      // --totem-pole
static bool fix_round7_arg;      // --fix-round7
static bool no_totem_attack_arg; // --no-totem-attack
static bool ending_arg;          // --ending (debug)
static int boss_door_arg = -1;   // --boss-door BOSSID (debug)
static const char* vram_path;    // --dump-vram FILE (headless test)
static constexpr uint64_t FRAME_NS = 16639267;   // NTSC: 60.0988 frames per second

// Turbo B (by default keyboard A, gamepad X / left button): while it's held, B is pressed for TURBO_HALF
// frames, released for TURBO_HALF, and so on. The game only acts on a new press of B, and a bat
// swing takes 17-18 frames, so a press every 6 frames starts the next swing as soon as the last
// one ends (measured headless: 34 swings in 10 s, the same as the fastest possible tapping).
static constexpr int TURBO_HALF = 3;
static int turbo_phase = -1;

// The first connected gamepad (any time one is plugged in or out, this picks again).
static void open_first_gamepad() {
    if (gamepad) return;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    if (ids && n > 0) gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

static bool pad_down(int code) {
    if (!gamepad || code == PAD_NONE) return false;
    if (code == PAD_LEFT_TRIGGER) return SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16384;
    if (code == PAD_RIGHT_TRIGGER) return SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
    return SDL_GetGamepadButton(gamepad, SDL_GamepadButton(code));
}

// 3D mode: the gamepad's right stick turns the camera like the mouse (the stick isn't used for the
// game). Called once a frame. Past a dead zone of a quarter of the way, the turn speed goes up with
// the square of how far the stick is pushed (fine control near the middle), up to 120 degrees a
// second. Both axes turn the camera the opposite way to the mouse (the user's choice): the stick
// pushed right turns it as the mouse moved left does, pushed up as the mouse moved down. In fast forward the frames come 4x as
// often, so each one turns a quarter as far.
static void gamepad_camera() {
    if (!gamepad) return;
    auto axis = [](Sint16 raw) {
        constexpr float DEAD = 0.25f;
        const float v = std::clamp(raw / 32767.0f, -1.0f, 1.0f), a = std::fabs(v);
        if (a <= DEAD) return 0.0f;
        const float t = (a - DEAD) / (1 - DEAD);
        return std::copysign(t * t, v);
    };
    const float sx = axis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    const float sy = axis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    if (sx == 0 && sy == 0) return;
    const float step = 2.0f / (fast_forward ? FAST_FORWARD : 1);   // degrees a frame at full tilt (60 fps: 120 a second)
    cam3d.yaw = std::clamp(cam3d.yaw - sx * step, -75.0f, 75.0f);     // (the mouse adds its x motion)
    cam3d.pitch = std::clamp(cam3d.pitch + sy * step, -60.0f, 60.0f); // (the mouse subtracts its y motion)
}

// 3D mode: clicking the right stick puts the camera back straight on (as a right click does), unless
// the right stick button is one of the game's controls (Controls...).
static bool right_stick_is_a_control() {
    for (int a = 0; a < IN_COUNT; a++)
        if (settings.input.pad[a] == SDL_GAMEPAD_BUTTON_RIGHT_STICK) return true;
    return false;
}

// The NES buttons held this frame, from the controls in the settings (keyboard and gamepad).
static uint8_t read_input() {
    static const uint8_t nes_button[IN_COUNT] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, 0, BTN_SELECT, BTN_START};
    const InputMap& m = settings.input;
    const bool* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    bool turbo = false;
    for (int a = 0; a < IN_COUNT; a++) {
        bool down = false;
        down |= (m.keys[a] > 0 && m.keys[a] < SDL_SCANCODE_COUNT && k[m.keys[a]]) || pad_down(m.pad[a]);
        if (!down) continue;
        if (a == IN_TURBO_B) turbo = true;
        else b |= nes_button[a];
    }
    if (gamepad && m.left_stick) {
        // left stick works as the d-pad (past half way)
        const Sint16 dead = 16384;
        const Sint16 sx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX), sy = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (sx < -dead) b |= BTN_LEFT;
        if (sx > dead) b |= BTN_RIGHT;
        if (sy < -dead) b |= BTN_UP;
        if (sy > dead) b |= BTN_DOWN;
    }
    if (turbo) {
        turbo_phase++;
        if ((turbo_phase / TURBO_HALF) % 2 == 0) b |= BTN_B;   // starts pressed, so the first swing is immediate
    } else {
        turbo_phase = -1;
    }
    if ((b & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) b &= ~(BTN_LEFT | BTN_RIGHT);
    if ((b & (BTN_UP | BTN_DOWN)) == (BTN_UP | BTN_DOWN)) b &= ~(BTN_UP | BTN_DOWN);
    return b;
}

static bool is_fullscreen() { return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN; }

static void set_fullscreen(bool on) {
    SDL_SetWindowFullscreen(window, on);
    if (on) SDL_HideCursor(); else SDL_ShowCursor();
}

// Draw the NES picture at a whole-number scale, centred, with black borders.
static void present(const uint32_t* pixels, int src_w) {
    int ow = 0, oh = 0;   // in pixels, so HiDPI screens stay sharp
    if (shader.loaded()) SDL_GetWindowSizeInPixels(window, &ow, &oh);
    else SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
    int w, h;
    if (settings.fill_screen) {
        // as big as fits, keeping the aspect ratio: usually top to bottom, not a whole-number scale
        const double aspect = double(picture_w(16, settings.aspect_8_7, settings.widescreen)) / picture_h(16);
        h = oh;
        w = int(std::lround(oh * aspect));
        if (w > ow) { w = ow; h = int(std::lround(ow / aspect)); }
    } else {
        int s = 1;
        while (picture_w(s + 1, settings.aspect_8_7, settings.widescreen) <= ow && picture_h(s + 1) <= oh) s++;
        if (is_fullscreen() && settings.fullscreen_scale > 0) s = std::min(s, settings.fullscreen_scale);
        w = picture_w(s, settings.aspect_8_7, settings.widescreen);
        h = picture_h(s);
    }
    if (shader.loaded()) {
        shader.render(pixels, src_w, 240, (ow - w) / 2, (oh - h) / 2, w, h, ow, oh);
        SDL_GL_SwapWindow(window);
        return;
    }
    SDL_UpdateTexture(texture, nullptr, pixels, src_w * 4);
    SDL_FRect dst{float((ow - w) / 2), float((oh - h) / 2), float(w), float(h)};
    SDL_Texture* src = texture;
    if (settings.fill_screen && (h % 240 != 0 || w % src_w != 0)) {
        // Not a whole-number scale: blow the picture up with sharp pixels to the next whole-number
        // size, then shrink that smoothly to the target. Pixels stay crisp, with at most a
        // 1-pixel soft edge between them, instead of the uneven widths plain nearest-neighbour gives.
        const int kx = (w + src_w - 1) / src_w, ky = (h + 239) / 240;
        if (!sharp_texture || sharp_w != src_w * kx || sharp_h != 240 * ky) {
            if (sharp_texture) SDL_DestroyTexture(sharp_texture);
            sharp_w = src_w * kx;
            sharp_h = 240 * ky;
            sharp_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, sharp_w, sharp_h);
            if (sharp_texture) SDL_SetTextureScaleMode(sharp_texture, SDL_SCALEMODE_LINEAR);
        }
        if (sharp_texture && SDL_SetRenderTarget(renderer, sharp_texture)) {
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_SetRenderTarget(renderer, nullptr);
            src = sharp_texture;
        }
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, src, nullptr, &dst);
    SDL_RenderPresent(renderer);
}

static SDL_DisplayID find_display(const std::string& name) {
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    SDL_DisplayID found = SDL_GetPrimaryDisplay();
    for (int i = 0; ids && i < n; i++) {
        const char* dn = SDL_GetDisplayName(ids[i]);
        if (dn && name == dn) { found = ids[i]; break; }
    }
    SDL_free(ids);
    return found;
}

// OpenGL context + shader preset. On failure everything is undone and the reason is in err.
static bool start_shader(const std::string& preset, std::string& err) {
    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) { err = std::string("no OpenGL context: ") + SDL_GetError(); return false; }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(0);   // frames are paced by the clock, as without shaders
    if (gl_load(err) && shader.load(preset, err)) return true;
    SDL_GL_DestroyContext(gl_context);
    gl_context = nullptr;
    return false;
}

// 3D mode: an OpenGL context and the 3D renderer (render3d.h).
static bool start_3d(std::string& err) {
    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) { err = std::string("no OpenGL context: ") + SDL_GetError(); return false; }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(0);   // frames are paced by the clock
    if (gl_load(err) && r3d.init(err)) return true;
    SDL_GL_DestroyContext(gl_context);
    gl_context = nullptr;
    return false;
}

static void present_3d(const Nes& nes) {
    int ow = 0, oh = 0;
    SDL_GetWindowSizeInPixels(window, &ow, &oh);
    r3d.draw(*scene3d, nes.frame, ow, oh, settings.aspect_8_7 ? 8.0f / 7.0f : 1.0f, cam3d);
    SDL_GL_SwapWindow(window);
}

// Open the game window as the settings say.
static bool open_game_window() {
    SDL_DisplayID disp = find_display(settings.display);
    const bool wide = settings.widescreen;
    int w = picture_w(settings.window_scale, settings.aspect_8_7, wide), h = picture_h(settings.window_scale);
    const std::string preset = mode3d ? std::string() : resolve_shader_path(settings.shader);   // no shaders in 3D
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (mode3d) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    }
    if (!preset.empty()) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);   // the shaders use GLSL 1.10/1.30
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }
    std::string title = rom_info(game_nes->game).title;
    if (!remix_text.empty()) title += " - Remix " + remix_text;
    window_title = title;
    fast_forward = false;
    window = SDL_CreateWindow(title.c_str(), w, h, flags);
    if (!window) return false;
    SDL_SetWindowMinimumSize(window, picture_w(1, settings.aspect_8_7, wide), picture_h(1));
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    if (settings.mode == DisplayMode::FullscreenExclusive) {
        SDL_DisplayMode mode;
        const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(disp);
        int mw = settings.exclusive_w ? settings.exclusive_w : desk ? desk->w : 0;
        int mh = settings.exclusive_h ? settings.exclusive_h : desk ? desk->h : 0;
        float hz = settings.exclusive_w ? settings.exclusive_hz : desk ? desk->refresh_rate : 0;
        if (SDL_GetClosestFullscreenDisplayMode(disp, mw, mh, hz, true, &mode)) SDL_SetWindowFullscreenMode(window, &mode);
        else std::fprintf(stderr, "no %dx%d video mode, using the desktop's: %s\n", mw, mh, SDL_GetError());
    } else {
        SDL_SetWindowFullscreenMode(window, nullptr);   // borderless at the desktop resolution
    }
    if (settings.mode != DisplayMode::Windowed) set_fullscreen(true);
    SDL_ShowWindow(window);
    if (mode3d) {
        std::string err;
        if (start_3d(err)) {
            capture_mouse(true);   // the mouse turns the camera
            return true;
        }
        std::fprintf(stderr, "3D mode: %s\n", err.c_str());
        std::string msg = "3D mode couldn't start (it needs OpenGL 3.0), so the game runs in 2D.\n\n" + err.substr(0, 1500);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: 3D mode", msg.c_str(), window);
        SDL_DestroyWindow(window);
        mode3d = false;
        game_nes->scene3d = nullptr;
        game_nes->wide_margin = settings.widescreen ? wide_margin(settings.aspect_8_7) : 0;
        return open_game_window();
    }
    if (!preset.empty()) {
        std::string err;
        if (start_shader(preset, err)) return true;
        std::fprintf(stderr, "shader %s: %s\n", preset.c_str(), err.c_str());
        std::string msg = "The shader couldn't be used, so the game runs without it.\n\n" + preset + "\n\n" + err.substr(0, 1500);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: shader", msg.c_str(), window);
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) return false;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                frame_width(settings.aspect_8_7, settings.widescreen), 240);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture != nullptr;
}

static bool on_frame_window(Nes& nes) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) return false;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_F12) return false;
            if (e.key.scancode == SDL_SCANCODE_ESCAPE && mode3d && mouse_captured) capture_mouse(false);   // (3D) first lets the mouse go
            else if (e.key.scancode == SDL_SCANCODE_ESCAPE && is_fullscreen()) set_fullscreen(false);
            else if (e.key.scancode == SDL_SCANCODE_ESCAPE) return false;
            if (e.key.scancode == SDL_SCANCODE_F10) nes.reset_requested = true;
            if (e.key.scancode == SDL_SCANCODE_TAB) {
                fast_forward = !fast_forward;
                SDL_SetWindowTitle(window, (window_title + (fast_forward ? " - Fast forward (4x)" : "")).c_str());
            }
            if (e.key.scancode == SDL_SCANCODE_F11 ||
                (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))) set_fullscreen(!is_fullscreen());
        }
        if (mode3d) {
            // 3D mode: the mouse turns the camera, the wheel zooms, a right click straightens it. Esc
            // lets the mouse go (in a window you need it back); a click in the window takes it again.
            if (!mouse_captured) {
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) capture_mouse(true);
            } else {
                if (e.type == SDL_EVENT_MOUSE_MOTION) {
                    cam3d.yaw = std::clamp(cam3d.yaw + e.motion.xrel * 0.2f, -75.0f, 75.0f);
                    cam3d.pitch = std::clamp(cam3d.pitch - e.motion.yrel * 0.2f, -60.0f, 60.0f);
                }
                if (e.type == SDL_EVENT_MOUSE_WHEEL) cam3d.zoom = std::clamp(cam3d.zoom * std::pow(0.9f, e.wheel.y), 0.3f, 3.0f);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) cam3d.reset();
            }
        }
        if (mode3d && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && gamepad && e.gbutton.which == SDL_GetGamepadID(gamepad) &&
            e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK && !right_stick_is_a_control())
            cam3d.reset();
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) open_first_gamepad();
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && e.gdevice.which == SDL_GetGamepadID(gamepad)) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
            open_first_gamepad();
        }
    }
    nes.buttons = read_input();
    if (mode3d) gamepad_camera();
    // fast forward: only every 4th frame is shown and heard (its sound at the normal pitch), the rest just run
    const bool shown = !fast_forward || nes.frame_count % FAST_FORWARD == 0;
    if (audio_stream && nes.audio_out && shown) {
        // keep at most ~100 ms queued so sound stays in step with the picture
        int queued = SDL_GetAudioStreamQueued(audio_stream);
        if (queued < int(NesAudio::SAMPLE_RATE / 10 * sizeof(int16_t)))
            SDL_PutAudioStreamData(audio_stream, nes.audio_out->data(), int(nes.audio_out->size() * sizeof(int16_t)));
    }
    if (shown) {
        if (fast_forward) {
            // the fast forward icon (graphics/fastforward.png) in the upper left corner, in the chosen palette's colours
            // (drawn on the finished frame; the next frame is drawn from scratch; not in 3D mode)
            const custom_sprite::Sprite& sp = custom_sprite::FAST_FORWARD;
            for (int y = 0; y < sp.h; y++)
                for (int x = 0; x < sp.w; x++)
                    if (const char c = sp.rows[y][x]; c != '.') nes.frame[(8 + y) * nes.frame_w + 8 + x] = nes.rgb[sp.colour(c) & 0x3F];
        }
        if (mode3d) present_3d(nes);
        else present(nes.frame, nes.frame_w);
    }
    uint64_t now = SDL_GetTicksNS();
    if (next_frame_ns > now) SDL_DelayNS(next_frame_ns - now);
    next_frame_ns = (next_frame_ns > now ? next_frame_ns : now) + (fast_forward ? FRAME_NS / FAST_FORWARD : FRAME_NS);
    return true;
}

// ---------------------------------------------------------------- headless test mode
struct Press { uint64_t frame; uint8_t buttons; };
static std::vector<Press> presses;
static uint64_t headless_frames;
struct Poke { uint64_t frame; uint16_t addr; uint8_t value; };
static std::vector<Poke> pokes;   // --poke FRAME:ADDR=VALUE (hex address and value), for tests
static std::vector<uint64_t> reset_frames;
static std::vector<int16_t> wav_samples;
static bool want_wav;

static bool on_frame_headless(Nes& nes) {
    if (want_wav && nes.audio_out) wav_samples.insert(wav_samples.end(), nes.audio_out->begin(), nes.audio_out->end());
    nes.buttons = 0;
    for (uint64_t f : reset_frames) if (nes.frame_count == f) nes.reset_requested = true;
    for (auto& p : pokes) if (nes.frame_count == p.frame) nes.cpu.ram[p.addr & 0x7FF] = p.value;
    for (auto& p : presses)
        if (nes.frame_count >= p.frame && nes.frame_count < p.frame + 8) nes.buttons |= p.buttons;
    return nes.frame_count < headless_frames;
}

// the last frame as a 24-bit BMP (headless --screenshot), at the game's pixel size
static bool write_bmp(const char* path, const Nes& nes) {
    const int w = nes.frame_w, h = 240, row = (w * 3 + 3) & ~3;
    FILE* f = mp_fopen(path, "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("BM", 1, 2, f); u32(54 + row * h); u32(0); u32(54);
    u32(40); u32(w); u32(h); u16(1); u16(24); u32(0); u32(row * h); u32(2835); u32(2835); u32(0); u32(0);
    std::vector<uint8_t> line(row, 0);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = nes.frame[y * w + x];
            line[x * 3] = c & 0xFF; line[x * 3 + 1] = (c >> 8) & 0xFF; line[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        std::fwrite(line.data(), 1, row, f);
    }
    return std::fclose(f) == 0;
}

// headless --3d --screenshot: the last frame drawn in 3D (an OpenGL window of its own, hidden)
static bool screenshot_3d(const char* path, const Nes& nes, std::string& err) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { err = SDL_GetError(); return false; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    const int w = 1280, h = 720;
    window = SDL_CreateWindow("3D", w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { err = SDL_GetError(); return false; }
    if (!start_3d(err)) return false;
    r3d.draw(*scene3d, nes.frame, w, h, 1.0f, cam3d);
    std::vector<uint32_t> px(size_t(w) * h);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = mp_fopen(path, "wb");
    if (!f) { err = std::string("cannot write ") + path; return false; }
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const int row = w * 3;
    std::fwrite("BM", 1, 2, f); u32(54 + row * h); u32(0); u32(54);
    u32(40); u32(w); u32(h); u16(1); u16(24); u32(0); u32(row * h); u32(2835); u32(2835); u32(0); u32(0);
    std::vector<uint8_t> line(row);
    for (int y = 0; y < h; y++) {                     // GL rows go bottom up, like a BMP's
        for (int x = 0; x < w; x++) {
            uint32_t c = px[size_t(y) * w + x];
            line[x * 3] = c & 0xFF; line[x * 3 + 1] = (c >> 8) & 0xFF; line[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        std::fwrite(line.data(), 1, row, f);
    }
    return std::fclose(f) == 0;
}

static bool write_wav(const char* path, const std::vector<int16_t>& smp) {
    FILE* f = mp_fopen(path, "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t bytes = uint32_t(smp.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(NesAudio::SAMPLE_RATE); u32(NesAudio::SAMPLE_RATE * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fwrite(smp.data(), 2, smp.size(), f);
    std::fclose(f);
    return true;
}

static char text_char(uint8_t t) {   // the main font (tools/text/main.tbl)
    if (t >= 0x04 && t <= 0x0D) return char('0' + t - 0x04);
    if (t >= 0x0E && t <= 0x27) return char('A' + t - 0x0E);
    if (t >= 0x28 && t <= 0x41) return char('a' + t - 0x28);
    if (t == 0x01) return ' ';
    return '.';
}

static void dump(Nes& nes) {
    const uint8_t* r = nes.cpu.ram;
    std::printf("%s %s frames=%llu GameState=%d GameMode=$%02X ScreenId=%d RoundNumber=%d PlayerHealth=%d ScreenPage=%d PRG=%d\n",
                nes.game == Game::Proto ? "[prototype]" : "[US]",
                nes.difficulty == Difficulty::Casual ? "casual" : nes.difficulty == Difficulty::Nasty ? "nasty" : "normal", (unsigned long long)nes.frame_count, r[GameState], r[GameMode], r[ScreenId], r[RoundNumber],
                r[PlayerHealth], r[ScreenPage], nes.cpu.prg_bank);
    uint32_t h = 2166136261u;   // FNV-1a of all 2 KB of RAM, to compare runs exactly
    for (uint8_t v : nes.cpu.ram) h = (h ^ v) * 16777619u;
    std::printf("RoomId=%d BossRoomFlag=$%02X BossIdInRound=%d BossId=$%02X BossesLeft=%d BossDefeated=%02X %02X %02X\n", r[RoomId], r[BossRoomFlag],
                r[BossIdInRound], r[BossId], r[BossesLeft], r[BossDefeated], r[BossDefeated + 1], r[BossDefeated + 2]);
    std::printf("ram hash %08X\n", h);
    std::printf("PlayerForm=$%02X BertTimer=%d is_cheating=%d\n", r[PlayerForm], r[BertTimer], nes.is_cheating ? 1 : 0);
    std::printf("palette");
    for (int i = 0; i < 32; i++) std::printf(" %02X", nes.palette[i]);
    std::printf("\nchr banks %02X %02X ppuctrl %02X\n", nes.mmc1_chr0, nes.mmc1_chr1, nes.ppu_ctrl);
    for (int ntab = 0; ntab < 2; ntab++) {
        for (int row = 0; row < 30; row++) {
            char line[33]; int letters = 0;
            for (int col = 0; col < 32; col++) {
                uint8_t t = nes.vram[ntab * 0x400 + row * 32 + col];
                line[col] = text_char(t);
                if ((line[col] >= 'A' && line[col] <= 'Z') || (line[col] >= '0' && line[col] <= '9')) letters++;
            }
            line[32] = 0;
            if (letters >= 3) std::printf("NT%d row %2d |%s|\n", ntab, row, line);
        }
    }
}

// roms/<file>: in the current folder, next to the executable, then the repository's roms/
static std::string find_rom(const char* file, std::vector<std::string>* looked = nullptr) {
    std::vector<std::string> dirs = {"roms/"};
    if (const char* base = SDL_GetBasePath()) dirs.push_back(std::string(base) + "roms/");
#ifdef MP_ROM_DIR
    dirs.push_back(std::string(MP_ROM_DIR) + "/");
#endif
    for (auto& d : dirs) {
        std::string p = d + file;
        if (FILE* f = mp_fopen(p, "rb")) { std::fclose(f); return p; }
        if (looked) looked->push_back(d);
    }
    return "";
}

static void rom_error(bool headless, const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Monster Party: ROM needed", msg.c_str(), nullptr);
}

// The Totem Pole's bubble attack (prototype only; Cpu::totem_fire in remix.cpp): after the remix
// (which starts the run-time part afresh), since the bubbles need that run-time part.
static void setup_totem_attack(Nes& nes, bool on) {
    if (!on || nes.game != Game::Proto) return;
    enable_remix_runtime(nes.prg, nes.game, nes.remix);
    nes.remix.totem_attack = true;
}

int main(int argc, char** argv) {
    mp_setup_console();   // Windows: the console it was started from, or monsterparty.log
    static Nes nes;
    game_nes = &nes;
    std::string err;
    const char* us_arg = argc > 1 && argv[1][0] != '-' ? argv[1] : nullptr;   // optional US ROM path
    bool want_proto = false;
    const char* language_arg = nullptr;   // --language, else the saved setting
    bool lang_report = false;             // --lang-report
    int difficulty_arg = -1;   // --difficulty, else the saved setting
    Trainers trainers_arg;     // --trainers
    bool have_trainers_arg = false;
    const char* lb_file_arg = nullptr;   // --leaderboards FILE
    bool no_lb_arg = false;              // --no-leaderboards
    const char* soundfont_arg = nullptr;
    const char* midi_out = nullptr;
    bool headless = false, want_dump = false, force_launcher = false, no_launcher = false, force_wide = false; const char* wav_path = nullptr;
    const char* shot_path = nullptr;
    bool want_3d = false;
    const char* palette_path = nullptr;
    for (int i = us_arg ? 2 : 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--headless") && i + 1 < argc) { headless = true; headless_frames = std::strtoull(argv[++i], nullptr, 10); }
        else if (!std::strcmp(argv[i], "--dump")) want_dump = true;
        else if (!std::strcmp(argv[i], "--prototype")) want_proto = true;
        else if (!std::strcmp(argv[i], "--language") && i + 1 < argc) language_arg = argv[++i];
        else if (!std::strcmp(argv[i], "--lang-report")) lang_report = true;
        else if (!std::strcmp(argv[i], "--soundfont") && i + 1 < argc) soundfont_arg = argv[++i];
        else if (!std::strcmp(argv[i], "--midi") && i + 1 < argc) midi_out = argv[++i];
        else if (!std::strcmp(argv[i], "--difficulty") && i + 1 < argc) {
            std::string d = argv[++i];
            difficulty_arg = d == "casual" ? 0 : d == "nasty" ? 2 : d == "normal" ? 1 : -1;
            if (difficulty_arg < 0) { std::fprintf(stderr, "--difficulty: casual, normal or nasty\n"); return 1; }
        }
        else if (!std::strcmp(argv[i], "--no-launcher")) no_launcher = true;
        else if (!std::strcmp(argv[i], "--leaderboards") && i + 1 < argc) lb_file_arg = argv[++i];
        else if (!std::strcmp(argv[i], "--no-leaderboards")) no_lb_arg = true;
        else if (!std::strcmp(argv[i], "--widescreen")) force_wide = true;
        else if (!std::strcmp(argv[i], "--3d")) want_3d = true;
        else if (!std::strcmp(argv[i], "--camera") && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f,%f", &cam3d.yaw, &cam3d.pitch, &cam3d.zoom) < 2) { std::fprintf(stderr, "--camera YAW,PITCH[,ZOOM]\n"); return 1; }
        }
        else if (!std::strcmp(argv[i], "--remix") && i + 1 < argc) remix_arg = argv[++i];
        else if (!std::strcmp(argv[i], "--totem-pole")) totem_pole_arg = true;
        else if (!std::strcmp(argv[i], "--fix-round7")) fix_round7_arg = true;
        else if (!std::strcmp(argv[i], "--no-totem-attack")) no_totem_attack_arg = true;
        else if (!std::strcmp(argv[i], "--dump-vram") && i + 1 < argc) vram_path = argv[++i];
        else if (!std::strcmp(argv[i], "--ending")) ending_arg = true;
        else if (!std::strcmp(argv[i], "--boss-door") && i + 1 < argc) boss_door_arg = int(std::strtol(argv[++i], nullptr, 16));
        else if (!std::strcmp(argv[i], "--enter-boss-door")) {
            // headless: start a game and go through round 1's first boss door (walk right, then put
            // the player in front of the door and press Up), then press A to get past speech bubbles
            for (uint64_t f : {200, 500, 800}) presses.push_back({f, BTN_START});
            for (uint64_t f = 1000; f <= 1342; f += 8) presses.push_back({f, BTN_RIGHT});
            for (uint64_t f : {1351, 1352}) { pokes.push_back({f, 0x3CB, 0x90}); pokes.push_back({f, 0x3D6, 0x8A}); }
            presses.push_back({1352, BTN_UP});
            for (uint64_t f = 1380; f <= 1700; f += 20) presses.push_back({f, BTN_A});
        }
        else if (!std::strcmp(argv[i], "--poke") && i + 1 < argc) {
            unsigned long long f; unsigned a, v;
            if (std::sscanf(argv[++i], "%llu:%x=%x", &f, &a, &v) != 3) { std::fprintf(stderr, "--poke FRAME:ADDR=VALUE (hex)\n"); return 1; }
            pokes.push_back({f, uint16_t(a), uint8_t(v)});
        }
        else if (!std::strcmp(argv[i], "--trainers") && i + 1 < argc) {
            std::string list = argv[++i];
            for (size_t p = 0; p <= list.size();) {
                size_t e = list.find(',', p); if (e == std::string::npos) e = list.size();
                const std::string t = list.substr(p, e - p);
                p = e + 1;
                if (t == "life") trainers_arg.infinite_life = true;
                else if (t == "full") trainers_arg.full_life = true;
                else if (t == "super") trainers_arg.super_jump = true;
                else if (t == "moon") trainers_arg.moon_jump = true;
                else if (t == "bert") trainers_arg.always_bert = true;
                else if (t == "doors") trainers_arg.mark_doors = true;
                else if (t.rfind("round=", 0) == 0 && std::atoi(t.c_str() + 6) >= 2 && std::atoi(t.c_str() + 6) <= 8) trainers_arg.start_round = std::atoi(t.c_str() + 6);
                else if (!t.empty()) { std::fprintf(stderr, "--trainers: life, full, round=N (2-8), super, moon, bert, doors\n"); return 1; }
            }
            have_trainers_arg = true;
        }
        else if (!std::strcmp(argv[i], "--palette") && i + 1 < argc) palette_path = argv[++i];
        else if (!std::strcmp(argv[i], "--reset") && i + 1 < argc) reset_frames.push_back(std::strtoull(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) { wav_path = argv[++i]; want_wav = true; }
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot_path = argv[++i];
        else if (!std::strcmp(argv[i], "--press") && i + 1 < argc) {
            const char* s = argv[++i]; Press p{std::strtoull(s, nullptr, 10), 0};
            const char* c = std::strchr(s, ':');
            for (c = c ? c + 1 : ""; *c; c++)
                p.buttons |= *c == 'A' ? BTN_A : *c == 'B' ? BTN_B : *c == 's' ? BTN_SELECT : *c == 'S' ? BTN_START :
                             *c == 'u' ? BTN_UP : *c == 'd' ? BTN_DOWN : *c == 'l' ? BTN_LEFT : *c == 'r' ? BTN_RIGHT : 0;
            presses.push_back(p);
        }
    }
    // ---- the ROMs: the US one is required; the Japanese prototype is optional
    const RomInfo& us = rom_info(Game::US);
    const RomInfo& jp = rom_info(Game::Proto);
    std::vector<std::string> looked;
    std::string us_path = us_arg ? us_arg : find_rom(us.file, &looked);
    std::string us_err;
    if (us_path.empty()) {
        std::string where;
        for (auto& d : looked) where += "\n  " + d;
        us_err = std::string("\"") + us.file + "\" wasn't found. Looked in:" + where;
    }
    if (!us_path.empty() && check_rom(us_path, Game::US, us_err)) us_err.clear();
    else if (us_err.empty()) us_err = "unknown error";
    if (!us_err.empty()) {
        rom_error(headless, std::string("This game needs the Monster Party (USA) ROM, named \"") + us.file +
                            "\", in a folder called roms next to the game.\n\n" + us_err);
        return 1;
    }
    std::string jp_path = find_rom(jp.file), jp_err;
    bool jp_ok = !jp_path.empty() && check_rom(jp_path, Game::Proto, jp_err);
    if (jp_path.empty()) jp_err = std::string("\"") + jp.file + "\" wasn't found in the roms folder.";

    auto load_game = [&](Nes& n, bool proto) {
        Game g = proto && jp_ok ? Game::Proto : Game::US;
        if (!n.load(g == Game::Proto ? jp_path : us_path, g, err)) { rom_error(headless, err); return false; }
        return true;
    };
    Language language_cli = Language::Default;
    if (language_arg && !parse_language(language_arg, language_cli)) { std::fprintf(stderr, "--language: default, en or ja\n"); return 1; }
    // the other game's ROM, for the Language setting's font (empty if it isn't there)
    auto other_rom = [&](const Nes& n) { return n.game == Game::Proto ? us_path : (jp_ok ? jp_path : std::string()); };
    if (lang_report) {
        // both translations, laid out as the games will show them
        int status = 0;
        for (Game g : {Game::US, Game::Proto}) {
            if (g == Game::Proto && !jp_ok) { std::printf("(the prototype's ROM isn't there: %s)\n", jp_err.c_str()); status = 1; continue; }
            auto np = std::make_unique<Nes>();
            Nes& n = *np;
            if (!load_game(n, g == Game::Proto)) return 1;
            std::string rep, lerr;
            if (!apply_language(n, g == Game::Proto ? Language::English : Language::Japanese, other_rom(n), lerr, &rep)) {
                std::printf("%s\n", lerr.c_str()); status = 1; continue;
            }
            std::printf("%s\n", rep.c_str());
        }
        return status;
    }
    if (headless) {
        if (want_proto && !jp_ok) { std::fprintf(stderr, "prototype: %s\n", jp_err.c_str()); return 1; }
        if (!load_game(nes, want_proto)) return 1;
        std::printf("%s", apply_bosses(nes.prg, nes.game, totem_pole_arg, fix_round7_arg).c_str());
        if (remix_arg) {
            uint32_t seed;
            if (!parse_remix_seed(remix_arg, seed)) { std::fprintf(stderr, "--remix: 8 hex digits, e.g. 1A2B-3C4D\n"); return 1; }
            std::printf("Remix %s\n%s", remix_seed_text(seed).c_str(), apply_remix(nes.prg, nes.game, seed, true, nes.remix).c_str());
        }
        if (boss_door_arg >= 0) std::printf("%s", debug_boss_door(nes.prg, nes.game, uint8_t(boss_door_arg), nes.remix).c_str());
        setup_totem_attack(nes, (totem_pole_arg || boss_door_arg == 0x1B) && !no_totem_attack_arg);
        if (!apply_language(nes, language_cli, other_rom(nes), err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    }

    auto to_difficulty = [](int d) { return d == 0 ? Difficulty::Casual : d == 2 ? Difficulty::Nasty : Difficulty::Normal; };
    // is_cheating: command-line debug options that change the game
    nes.debug_used = boss_door_arg >= 0 || !pokes.empty() || ending_arg;   // --enter-boss-door works with pokes
    nes.debug_ending = ending_arg;
    if (headless) {
        nes.trainers = trainers_arg;
        // leaderboards: only with --leaderboards FILE (a run with --remix has a fixed seed: not logged)
        nes.lb.enabled = lb_file_arg && !no_lb_arg && !remix_arg;
        nes.lb.path = lb_file_arg ? lb_file_arg : "";
        nes.lb.category = LbCategory{g_is_proto(nes), difficulty_arg < 0 ? 1 : difficulty_arg, false};
        nes.reset();
        nes.difficulty = to_difficulty(difficulty_arg < 0 ? 1 : difficulty_arg);
        if (soundfont_arg) {
            std::string serr;
            if (!nes.music.load_soundfont(soundfont_arg, serr)) { std::fprintf(stderr, "%s\n", serr.c_str()); return 1; }
        }
        if (midi_out) nes.music.start_recording();
        if (force_wide) nes.wide_margin = wide_margin(false);
        nes.wide_dim = force_wide;
        if (want_3d) {
            scene3d = new Scene3D;
            scene3d->wide = force_wide;
            nes.scene3d = scene3d;
            if (!force_wide) nes.wide_margin = 16;   // a little room map around the screen (3D needs a room view)
        }
        if (palette_path) {
            std::string perr;
            if (!load_palette(palette_path, nes.rgb, nullptr, perr)) { std::fprintf(stderr, "%s\n", perr.c_str()); return 1; }
        }
        nes.on_frame = on_frame_headless;
        nes.run();
        if (want_dump) dump(nes);
        if (vram_path) {
            if (FILE* f = mp_fopen(vram_path, "wb")) {
                const uint8_t regs[3] = {nes.mmc1_chr0, nes.mmc1_chr1, nes.ppu_ctrl};
                std::fwrite(nes.vram, 1, sizeof nes.vram, f); std::fwrite(nes.palette, 1, 32, f); std::fwrite(regs, 1, 3, f);
                std::fclose(f);
            }
        }
        if (shot_path && want_3d) {
            std::string err;
            if (!screenshot_3d(shot_path, nes, err)) { std::fprintf(stderr, "3D screenshot: %s\n", err.c_str()); return 1; }
        } else if (shot_path && !write_bmp(shot_path, nes)) { std::fprintf(stderr, "cannot write %s\n", shot_path); return 1; }
        if (midi_out) {
            std::string merr;
            if (!nes.music.save_recording(midi_out, merr)) { std::fprintf(stderr, "%s\n", merr.c_str()); return 1; }
        }
        if (wav_path) {
            double sum = 0; for (int16_t v : wav_samples) sum += double(v) * v;
            std::printf("wav: %zu samples, rms %.0f\n", wav_samples.size(),
                        wav_samples.empty() ? 0.0 : std::sqrt(sum / wav_samples.size()));
            if (!write_wav(wav_path, wav_samples)) { std::fprintf(stderr, "cannot write %s\n", wav_path); return 1; }
        }
        return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    // no sound device (or no sound system SDL knows): play silently rather than not at all
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
    // A game session: the settings window, then the game until it's left (Esc, F12, closing the
    // window), then back to the settings window. Quit there ends the program. Command-line options
    // apply to the first session only. (--no-launcher: just one session.)
    bool first = true;
    auto session = [&](Nes& nes) -> bool {
        load_settings(settings);
        settings.palette = upgrade_palette_setting(settings.palette);   // bundled palettes were .txt files before
        if (want_proto) settings.prototype = true;
        // the settings window: at start (unless skipped), and again each time the game is left
        if (((settings.show_launcher || force_launcher) && !no_launcher) || !first) {
            if (!run_launcher(settings, jp_ok, jp_err)) return false;
            if (!save_settings(settings)) std::fprintf(stderr, "cannot save settings to %s\n", settings_path().c_str());
        }
        if (settings.prototype && !jp_ok) {
            std::string msg = "The Japanese prototype can't be played, so the US version starts instead.\n\n" + jp_err;
            std::fprintf(stderr, "%s\n", msg.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: prototype", msg.c_str(), nullptr);
        }
        if (!load_game(nes, settings.prototype)) return false;
        // debug options that change the game (is_cheating); this is a fresh Nes, so they're set here too
        nes.debug_used = boss_door_arg >= 0 || !pokes.empty() || ending_arg;
        nes.debug_ending = ending_arg;
        // Remix Mode: a seed for this session (typed in the launcher, or new), applied to the loaded ROM
        if (remix_arg) { settings.remix = true; settings.remix_seed = remix_arg; }
        // Bosses options (before the remix, which shuffles the doors as they are then)
        if (totem_pole_arg) settings.totem_pole = true;
        if (fix_round7_arg) settings.fix_round7_key = true;
        std::fprintf(stderr, "%s", apply_bosses(nes.prg, nes.game, settings.totem_pole, settings.fix_round7_key).c_str());
        if (settings.remix) {
            uint32_t seed;
            if (settings.remix_seed.empty() || !parse_remix_seed(settings.remix_seed, seed)) {
                if (!settings.remix_seed.empty()) std::fprintf(stderr, "remix: \"%s\" isn't a seed, using a random one\n", settings.remix_seed.c_str());
                seed = new_remix_seed();
            }
            remix_text = remix_seed_text(seed);
            std::fprintf(stderr, "Remix %s\n%s", remix_text.c_str(),
                         apply_remix(nes.prg, nes.game, seed, true, nes.remix).c_str());
            if (!remix_arg && settings.remix_last_seed != remix_text) {
                settings.remix_last_seed = remix_text;
                save_settings(settings);
            }
        }
        if (boss_door_arg >= 0) std::fprintf(stderr, "%s", debug_boss_door(nes.prg, nes.game, uint8_t(boss_door_arg), nes.remix).c_str());
        if (no_totem_attack_arg) settings.totem_attack = false;
        setup_totem_attack(nes, (settings.totem_pole || boss_door_arg == 0x1B) && settings.totem_attack);
        // Language (drawing only)
        if (language_arg) settings.language = language_setting(language_cli);
        {
            Language lang = Language::Default;
            parse_language(settings.language, lang);
            std::string lerr;
            if (!apply_language(nes, lang, other_rom(nes), lerr)) {
                std::string msg = "The game starts in its own language.\n\n" + lerr;
                std::fprintf(stderr, "%s\n", msg.c_str());
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: language", msg.c_str(), nullptr);
            }
        }
        // Trainers (cheats), from the launcher or --trainers
        if (have_trainers_arg) nes.trainers = trainers_arg;
        else {
            nes.trainers.infinite_life = settings.trainer_infinite_life;
            nes.trainers.full_life = settings.trainer_full_life;
            nes.trainers.start_round = settings.trainer_start_round_on ? std::clamp(settings.trainer_start_round, 2, 8) : 0;
            nes.trainers.moon_jump = settings.trainer_moon_jump;
            nes.trainers.super_jump = settings.trainer_super_jump && !settings.trainer_moon_jump;
            nes.trainers.always_bert = settings.trainer_always_bert;
            nes.trainers.mark_doors = settings.trainer_mark_doors;
        }
        // leaderboards: a fixed (typed or --remix) Remix seed makes every run the same game, so it isn't logged
        if (difficulty_arg >= 0) settings.difficulty = difficulty_arg;
        {
            uint32_t fixed_seed;
            const bool fixed = settings.remix && (remix_arg || (!settings.remix_seed.empty() && parse_remix_seed(settings.remix_seed, fixed_seed)));
            nes.lb.enabled = settings.leaderboards && !no_lb_arg && !fixed;
            nes.lb.path = lb_file_arg ? lb_file_arg : leaderboards_path();
            nes.lb.category = LbCategory{g_is_proto(nes), settings.difficulty, settings.remix};
        }
        nes.reset();
        {
            // (a game from the password screen is a cheat too: that's only known once the title screen is done)
            const char* why = !settings.leaderboards || no_lb_arg ? "off in the settings" : !nes.lb.enabled ? "off: fixed Remix seed" : nes.is_cheating ? "off: cheats or debug options in use" : "";
            std::fprintf(stderr, "leaderboard: [%s] -> %s%s%s%s\n", nes.lb.category.title().c_str(),
                            nes.lb.enabled && !nes.is_cheating ? nes.lb.path.c_str() : "no runs will be logged",
                            *why ? " (" : "", why, *why ? ")" : "" 
                        );
        }
        nes.difficulty = to_difficulty(settings.difficulty);
        // music: the NES sound chip, or MIDI through a SoundFont
        if (soundfont_arg) { settings.music_off = false; settings.music_midi = true; settings.soundfont = soundfont_arg; }
        nes.music_off = settings.music_off;
        if (settings.music_midi && !settings.music_off && settings.sound) {
            std::string sf = settings.soundfont;
            if (sf.empty()) { auto all = list_soundfonts(); if (!all.empty()) sf = all[0].setting; }
            std::string serr = sf.empty() ? std::string("No SoundFont (.sf2) was found. Put one in ") + user_soundfont_dir() : std::string();
            nes.music.programs = {settings.midi_program[0], settings.midi_program[1], settings.midi_program[2]};
            if (sf.empty() || !nes.music.load_soundfont(resolve_soundfont_path(sf), serr)) {
                std::string msg = "The MIDI music couldn't start, so the music plays on the NES sound chip.\n\n" + serr;
                std::fprintf(stderr, "%s\n", msg.c_str());
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: music", msg.c_str(), nullptr);
            }
        }
        if (force_wide) settings.widescreen = true;
        if (palette_path) settings.palette = palette_path;
        if (!settings.palette.empty()) {
            std::string perr, path = resolve_palette_path(settings.palette);
            if (!load_palette(path, nes.rgb, nullptr, perr)) {
                std::fprintf(stderr, "palette: %s\n", perr.c_str());
                std::string msg = "The palette couldn't be loaded, so the game uses the default colours.\n\n" + perr;
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Monster Party: palette", msg.c_str(), nullptr);
            }
        }
        if (settings.widescreen) nes.wide_margin = wide_margin(settings.aspect_8_7);
        nes.frame_w = frame_width(settings.aspect_8_7, settings.widescreen);
        if (want_3d) settings.mode3d = true;
        nes.wide_dim = settings.widescreen;   // (the extra sides dimmed while playing)
        if (settings.mode3d) {
            mode3d = true;
            scene3d = new Scene3D;
            scene3d->wide = settings.widescreen;   // the whole 16:9 frame, or just the game's own screen
            nes.scene3d = scene3d;
            if (!settings.widescreen) nes.wide_margin = 16;   // (3D needs a room view: a little room map around the screen, not shown)
        }
        if (!open_game_window()) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
        SDL_AudioSpec spec{SDL_AUDIO_S16, 1, NesAudio::SAMPLE_RATE};
        audio_stream = settings.sound ? SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr) : nullptr;
        if (audio_stream) SDL_ResumeAudioStreamDevice(audio_stream);
        else if (settings.sound) std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
        else std::fprintf(stderr, "sound: off (no audio output)\n");
        // The settings window has already taken the "gamepad added" events for pads that were
        // connected at startup, so look for one now rather than waiting for an event.
        open_first_gamepad();
        nes.on_frame = on_frame_window;
        next_frame_ns = SDL_GetTicksNS();
        nes.run();
        // close the game window and everything that goes with it
        if (audio_stream) { SDL_DestroyAudioStream(audio_stream); audio_stream = nullptr; }
        shader.destroy();   // needs the GL context, so before it goes
        if (gl_context) { SDL_GL_DestroyContext(gl_context); gl_context = nullptr; }
        if (sharp_texture) { SDL_DestroyTexture(sharp_texture); sharp_texture = nullptr; }
        if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
        if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
        if (window) { SDL_DestroyWindow(window); window = nullptr; }
        if (gamepad) { SDL_CloseGamepad(gamepad); gamepad = nullptr; }
        delete scene3d; scene3d = nullptr;
        mode3d = false; mouse_captured = false; cam3d.reset();
        remix_text.clear();
        return true;
    };
    for (;;) {
        auto nes = std::make_unique<Nes>();
        game_nes = nes.get();
        const bool more = session(*nes);
        if (!more || no_launcher) break;
        first = false;
        // the command line's options were for the first game only
        ending_arg = false; remix_arg = nullptr; totem_pole_arg = fix_round7_arg = no_totem_attack_arg = false; boss_door_arg = -1;
        have_trainers_arg = false; difficulty_arg = -1; soundfont_arg = nullptr; palette_path = nullptr;
        force_wide = want_3d = want_proto = false;
    }
    game_nes = nullptr;
    SDL_Quit();
    return 0;
}
