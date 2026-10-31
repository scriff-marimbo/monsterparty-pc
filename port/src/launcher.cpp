// The launcher: a small window with the video settings and a START button, drawn with Dear ImGui
// on SDL's renderer. Keyboard: Enter starts, Esc quits, arrows/Tab/Space move around the controls.
// A gamepad works too (d-pad and A to change things, Start to start).
#include "launcher.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include "leaderboard.h"
#include "shader_chain.h"
#include "palette.h"
#include "music.h"
#include "language.h"
#include "platform.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

namespace {

// Presets offered in the Shader list (paths inside libretro/glsl-shaders). Each one is listed only
// if its file is there. Any other .glslp or .glsl can be picked with "Other file...".
struct BundledShader { const char* label; const char* path; const char* tip; };
const BundledShader kShaders[] = {
    {"Sharp bilinear", "interpolation/sharp-bilinear-simple.glslp",
     "Crisp pixels with only the edges smoothed. Good with the 8:7 pixel shape. (public domain)"},
    {"Bicubic", "cubic/bicubic.glslp", "Smooth, soft scaling. (GPL 2+)"},
    {"NTSC (simple)", "ntsc/ntsc-simple.glslp", "Light composite-video colour bleed. (GPL 2+ and unlicensed parts)"},
    {"NTSC (adaptive)", "ntsc/ntsc-adaptive.glslp", "Composite video with dot crawl and fringing; adjusts to the game's width."},
    {"NTSC 256px + scanlines", "ntsc/ntsc-256px-gauss-scanline.glslp", "Composite video tuned for 256-pixel-wide consoles like the NES, with soft scanlines."},
    {"CRT Lottes", "crt/crt-lottes.glslp", "A light CRT look: scanlines, shadow mask, slight curvature. (public domain)"},
    {"CRT Royale, NTSC 256px composite", "crt/crt-royale-ntsc-256px-composite.glslp",
     "Very detailed CRT with composite video, tuned for 256-pixel-wide consoles like the NES.\nHeavy: 14 passes, needs a reasonable GPU. (GPL 2+)"},
    {"CRT Royale, NTSC 320px composite", "crt/crt-royale-ntsc-320px-composite.glslp",
     "As above, but the composite pass is tuned for 320-pixel-wide consoles (2-phase NTSC).\nHeavy: 14 passes. (GPL 2+)"},
};

const char* APP_VERSION = "1.0.0";

// ---- the Controls popup
struct Listen { int action = -1; bool pad = false; };   // which box is waiting for a key/button

// Put code into one box, and take it out of every other box of the same kind, so one key or
// button never does two things.
void assign_input(InputMap& m, const Listen& l, int code) {
    for (int a = 0; a < IN_COUNT; a++)
        if ((l.pad ? m.pad[a] : m.keys[a]) == code) (l.pad ? m.pad[a] : m.keys[a]) = l.pad ? PAD_NONE : 0;
    (l.pad ? m.pad : m.keys)[l.action] = code;
}

void controls_popup(Settings& s, Listen& listen, float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 24 * ui_scale, io.DisplaySize.y - 24 * ui_scale));
    if (!ImGui::BeginPopupModal("Controls", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;

    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    const char* pad_name = ids && n > 0 ? SDL_GetGamepadNameForID(ids[0]) : nullptr;
    SDL_free(ids);
    ImGui::TextDisabled("%s", n > 0 ? (std::string("Gamepad: ") + (pad_name ? pad_name : "connected")).c_str() : "No gamepad connected");
    ImGui::TextWrapped("Click a box, then press the key or gamepad button for it. Esc cancels, Delete clears the box.");
    ImGui::Spacing();

    const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##controls", 3, tf)) {
        ImGui::TableSetupColumn("NES", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Turbo B").x + 8 * ui_scale);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (int a = 0; a < IN_COUNT; a++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(input_action_name(a));
            if (a == IN_TURBO_B) ImGui::SetItemTooltip("Hold to keep pressing B (swings the bat as fast as the game allows).");
            for (int col = 0; col < 2; col++) {
                ImGui::TableNextColumn();
                const bool pad = col == 1;
                const bool waiting = listen.action == a && listen.pad == pad;
                std::string label = waiting ? (pad ? "Press a button..." : "Press a key...")
                                            : pad ? pad_display_name(s.input.pad[a]) : key_display_name(s.input.keys[a]);
                ImGui::PushID(a * 8 + col);
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.18f, 0.26f, 1.0f));
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) listen = waiting ? Listen{} : Listen{a, pad};
                if (waiting) ImGui::PopStyleColor();
                if (!waiting) ImGui::SetItemTooltip("%s", label.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Checkbox("Left stick also works as the d-pad", &s.input.left_stick);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Right stick: moves the camera in 3D mode (click it to put the camera back).");
    ImGui::TextWrapped("F10: Reset, F11: Fullscreen, F12: Quit, Esc: Leaves fullscreen or quits.");
    ImGui::PopStyleColor();

    const ImGuiStyle& st = ImGui::GetStyle();
    const float bw = 180 * ui_scale, bh = ImGui::GetFrameHeight() * 1.4f;
    ImGui::SetCursorPos(ImVec2(st.WindowPadding.x, ImGui::GetWindowHeight() - st.WindowPadding.y - bh));
    if (ImGui::Button("Reset to defaults", ImVec2(bw, bh))) { s.input = default_input_map(); listen = Listen{}; }
    ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - 100 * ui_scale);
    if (ImGui::Button("Done", ImVec2(100 * ui_scale, bh))) { listen = Listen{}; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

// ---- the MIDI instruments popup: General MIDI programs for the music's three melodic channels
const char* const GM_PROGRAMS[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"};

void instruments_popup(Settings& s, float ui_scale) {
    ImGui::SetNextWindowSize(ImVec2(470 * ui_scale, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("MIDI instruments", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::TextWrapped("The General MIDI instrument for each of the music's channels. The NES's noise channel always plays as drums.");
    ImGui::Spacing();
    static const char* const channels[3] = {"Pulse 1 (lead)", "Pulse 2", "Triangle (bass)"};
    const float lw = ImGui::CalcTextSize("Triangle (bass)").x + 24 * ui_scale;
    for (int i = 0; i < 3; i++) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(channels[i]);
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-FLT_MIN);
        int& prog = s.midi_program[i];
        prog = std::clamp(prog, 0, 127);
        char label[64];
        std::snprintf(label, sizeof label, "%d  %s", prog, GM_PROGRAMS[prog]);
        ImGui::PushID(i);
        if (ImGui::BeginCombo("##prog", label, ImGuiComboFlags_HeightLarge)) {
            for (int p = 0; p < 128; p++) {
                std::snprintf(label, sizeof label, "%d  %s", p, GM_PROGRAMS[p]);
                if (ImGui::Selectable(label, p == prog)) prog = p;
                if (p == prog && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Takes effect when the game starts, and is saved with the other settings. The sound depends on the SoundFont: try a few.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    const float bh = ImGui::GetFrameHeight() * 1.4f;
    if (ImGui::Button("Reset to defaults", ImVec2(180 * ui_scale, bh))) { Settings d; std::copy(d.midi_program, d.midi_program + 3, s.midi_program); }
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 100 * ui_scale);
    if (ImGui::Button("Done", ImVec2(100 * ui_scale, bh))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The local leaderboards (see leaderboard.h): one per ROM + difficulty + Remix on/off, each with the
// fastest times and the highest scores. Read from the file each time the window opens.
void leaderboards_popup(const Settings& s, float ui_scale) {
    ImGui::SetNextWindowSize(ImVec2(620 * ui_scale, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Leaderboards", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
    static LbCategory cat;
    static Leaderboards boards;
    if (ImGui::IsWindowAppearing()) {
        cat = LbCategory{s.prototype, std::clamp(s.difficulty, 0, 2), s.remix};
        boards.load(leaderboards_path());
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Show");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * ui_scale);
    if (ImGui::BeginCombo("##lbrom", cat.proto ? "JP prototype" : "US release")) {
        if (ImGui::Selectable("US release", !cat.proto)) cat.proto = false;
        if (ImGui::Selectable("JP prototype", cat.proto)) cat.proto = true;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    static const char* const diffs[3] = {"Casual", "Normal", "Nasty"};
    ImGui::SetNextItemWidth(110 * ui_scale);
    if (ImGui::BeginCombo("##lbdiff", diffs[cat.difficulty])) {
        for (int i = 0; i < 3; i++) if (ImGui::Selectable(diffs[i], cat.difficulty == i)) cat.difficulty = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Remix", &cat.remix);
    ImGui::Spacing();
    ImGui::SeparatorText(cat.title().c_str());
    const LbBoard& b = boards.get(cat);
    if (ImGui::BeginTable("##lbtable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30 * ui_scale);
        ImGui::TableSetupColumn("Fastest time");
        ImGui::TableSetupColumn("Date");
        ImGui::TableSetupColumn("Highest score");
        ImGui::TableSetupColumn("Date");
        ImGui::TableHeadersRow();
        for (int i = 0; i < LB_MAX; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
            const LbEntry* t = i < int(b.by_time.size()) ? &b.by_time[i] : nullptr;
            const LbEntry* sc = i < int(b.by_score.size()) ? &b.by_score[i] : nullptr;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(t ? lb_clock_cs(t->frames).c_str() : "-");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(t ? lb_date(t->when).c_str() : "");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(sc ? lb_score_digits(sc->score).c_str() : "-");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(sc ? lb_date(sc->when).c_str() : "");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("A game counts if it's a new game from the title screen with no password, trainer or debug option, and the "
                       "final boss is beaten. The time runs from the start of Round 1 (after ROUND 1 START), in game time. "
                       "Remix games with a typed seed aren't logged. After the credits, press A or Start to see the result screen.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    const float bh = ImGui::GetFrameHeight() * 1.4f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 100 * ui_scale);
    if (ImGui::Button("Done", ImVec2(100 * ui_scale, bh))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool exists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

// "Other file..." result; SDL may call back on another thread
std::mutex picked_mutex;
std::string picked_file;
void SDLCALL on_file_picked(void*, const char* const* files, int) {
    if (!files || !files[0]) return;   // cancelled or failed
    std::lock_guard<std::mutex> lock(picked_mutex);
    picked_file = files[0];
}

struct VideoMode { int w, h; float hz; float density; };

struct Monitor {
    SDL_DisplayID id;
    std::string name;
    SDL_Rect usable{};             // usable desktop area (screen coordinates), for the windowed size
    VideoMode desktop{};
    std::vector<VideoMode> modes;  // exclusive fullscreen modes, largest first, duplicates removed
};

std::vector<Monitor> list_monitors() {
    std::vector<Monitor> out;
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; i++) {
        Monitor m;
        m.id = ids[i];
        const char* name = SDL_GetDisplayName(ids[i]);
        m.name = name ? name : "Display " + std::to_string(i + 1);
        if (!SDL_GetDisplayUsableBounds(ids[i], &m.usable)) SDL_GetDisplayBounds(ids[i], &m.usable);
        if (const SDL_DisplayMode* d = SDL_GetDesktopDisplayMode(ids[i]))
            m.desktop = {d->w, d->h, d->refresh_rate, d->pixel_density > 0 ? d->pixel_density : 1.0f};
        int nm = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(ids[i], &nm);
        for (int j = 0; modes && j < nm; j++) {
            const SDL_DisplayMode* d = modes[j];
            bool dup = false;
            for (auto& v : m.modes) dup |= v.w == d->w && v.h == d->h && std::fabs(v.hz - d->refresh_rate) < 0.01f;
            if (!dup) m.modes.push_back({d->w, d->h, d->refresh_rate, d->pixel_density > 0 ? d->pixel_density : 1.0f});
        }
        SDL_free(modes);
        out.push_back(m);
    }
    SDL_free(ids);
    return out;
}

// largest whole-number scale whose picture fits in w x h
int fit_scale(int w, int h, bool a87, bool wide) {
    int s = 1;
    while (picture_w(s + 1, a87, wide) <= w && picture_h(s + 1) <= h) s++;
    return s;
}

std::string mode_label(const VideoMode& v) {
    char b[64];
    if (v.hz > 0) std::snprintf(b, sizeof b, "%d x %d  @ %.4g Hz", int(v.w * v.density), int(v.h * v.density), v.hz);
    else std::snprintf(b, sizeof b, "%d x %d", int(v.w * v.density), int(v.h * v.density));
    return b;
}

// label column + control on the same line
void row_label(const char* label, float label_w) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_w);
}

void apply_style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(18, 16);
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 8);
    st.FrameRounding = 4;
    st.GrabRounding = 4;
    st.PopupRounding = 4;
    st.WindowBorderSize = 0;
    ImVec4* c = st.Colors;
    const ImVec4 accent(0.80f, 0.22f, 0.30f, 1.0f), accent_hi(0.90f, 0.32f, 0.40f, 1.0f), accent_lo(0.62f, 0.15f, 0.22f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.17f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.26f, 0.35f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.27f, 0.35f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.32f, 0.42f, 1.0f);
    c[ImGuiCol_Header] = accent_lo;
    c[ImGuiCol_HeaderHovered] = accent;
    c[ImGuiCol_HeaderActive] = accent_hi;
    c[ImGuiCol_CheckMark] = accent_hi;
    c[ImGuiCol_Separator] = ImVec4(0.28f, 0.25f, 0.32f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.13f, 0.17f, 1.0f);
    c[ImGuiCol_NavCursor] = accent_hi;
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 16.0f;
    st.FontScaleDpi = scale;
}

}  // namespace

namespace {
// The Language list's "日本語": ImGui's own font has no kanji, so a Japanese font from the system
// is merged in for them if there is one (else it says "Japanese").
bool japanese_label_ok = false;
bool add_japanese_font() {
    static const char* const paths[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/noto-cjk/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf",
        "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
        "/usr/share/fonts/ipa-gothic/ipag.ttf",
        "/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf",
        "/usr/share/fonts/vlgothic/VL-Gothic-Regular.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf",
        "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    };
    ImGuiIO& io = ImGui::GetIO();
    for (const char* p : paths) {
        if (!exists(p)) continue;
        io.Fonts->AddFontDefault();
        ImFontConfig cfg;
        cfg.MergeMode = true;   // only for what the default font hasn't got
        if (io.Fonts->AddFontFromFileTTF(p, 0.0f, &cfg)) return true;
    }
    return false;
}

}  // namespace

bool run_launcher(Settings& s, bool proto_ok, const std::string& proto_why) {
    std::vector<Monitor> mons = list_monitors();
    if (mons.empty()) return true;   // nothing to choose from: just start with what we have

    int mon = 0;
    for (size_t i = 0; i < mons.size(); i++) if (mons[i].name == s.display) mon = int(i);
    if (s.display.empty()) {
        SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (size_t i = 0; i < mons.size(); i++) if (mons[i].id == primary) mon = int(i);
    }

    float ui_scale = SDL_GetDisplayContentScale(mons[mon].id);
    if (ui_scale <= 0) ui_scale = 1;
    SDL_Window* window = SDL_CreateWindow("Monster Party: Let's Go Again", int(1120 * ui_scale), int(690 * ui_scale),
                                          SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { std::fprintf(stderr, "launcher window: %s\n", SDL_GetError()); return true; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { std::fprintf(stderr, "launcher renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(window); return true; }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(mons[mon].id), SDL_WINDOWPOS_CENTERED_DISPLAY(mons[mon].id));
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;    // don't write imgui.ini next to the game
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    apply_style(ui_scale);
    japanese_label_ok = add_japanese_font();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // which exclusive mode is selected: 0 = the desktop's own mode, i+1 = modes[i]
    auto find_mode = [&](const Monitor& m) {
        for (size_t i = 0; i < m.modes.size(); i++)
            if (m.modes[i].w == s.exclusive_w && m.modes[i].h == s.exclusive_h &&
                (s.exclusive_hz == 0 || std::fabs(m.modes[i].hz - s.exclusive_hz) < 0.05f)) return int(i) + 1;
        return 0;
    };
    int mode_sel = find_mode(mons[mon]);

    // shaders that are actually there
    // palettes: the list is re-read whenever the drop-down opens, so new files show up
    std::vector<PaletteFile> palettes = list_palettes();
    std::vector<SoundFontFile> soundfonts = list_soundfonts();   // re-read when the Music list opens
    std::string preview_for = "\x01";   // which palette `preview` holds
    uint32_t preview[64];
    std::string palette_error;

    const std::string shader_dir = shader_root();
    std::vector<const BundledShader*> shaders;
    if (!shader_dir.empty())
        for (const auto& b : kShaders) if (exists(shader_dir + b.path)) shaders.push_back(&b);

    bool done = false, start = false, popup_was_open = false;
    Listen listen;
    int pad_nav_cooldown = 0;   // frames to keep ImGui's gamepad navigation off after a button was taken for a control
    // open every gamepad now, so the Controls popup gets their button presses
    std::vector<SDL_Gamepad*> pads;
    {
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        for (int i = 0; ids && i < n; i++) if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i])) pads.push_back(g);
        SDL_free(ids);
    }
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED)
                if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) pads.push_back(g);
            if (listen.action >= 0) {
                // a Controls box is waiting: keys and gamepad buttons go to it, not to the menu
                if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    if (e.key.scancode == SDL_SCANCODE_ESCAPE) listen = Listen{};
                    else if (e.key.scancode == SDL_SCANCODE_DELETE) {
                        (listen.pad ? s.input.pad : s.input.keys)[listen.action] = listen.pad ? PAD_NONE : 0;
                        listen = Listen{};
                    } else if (!listen.pad && e.key.scancode != SDL_SCANCODE_TAB) { assign_input(s.input, listen, int(e.key.scancode)); listen = Listen{}; }   // (Tab is fast forward)
                    continue;
                }
                if (e.type == SDL_EVENT_KEY_UP) continue;
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                    int code = PAD_NONE;
                    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) code = e.gbutton.button;
                    else if (e.gaxis.value > 16384 && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) code = PAD_LEFT_TRIGGER;
                    else if (e.gaxis.value > 16384 && e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) code = PAD_RIGHT_TRIGGER;
                    if (listen.pad && code != PAD_NONE) { assign_input(s.input, listen, code); listen = Listen{}; pad_nav_cooldown = 30; }
                    continue;
                }
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) done = true;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START &&
                !ImGui::IsPopupOpen("Controls")) { start = done = true; }
        }
        // ImGui reads the gamepad directly: keep it from also acting on a button just taken for a control
        if (listen.action >= 0 || pad_nav_cooldown > 0) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (pad_nav_cooldown > 0) pad_nav_cooldown--;
        {
            std::lock_guard<std::mutex> lock(picked_mutex);
            if (!picked_file.empty()) {
                s.shader = picked_file;
                if (!shader_dir.empty() && s.shader.rfind(shader_dir, 0) == 0) s.shader = s.shader.substr(shader_dir.size());
                picked_file.clear();
            }
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(10); continue; }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const Monitor& m = mons[mon];
        const bool full = s.mode != DisplayMode::Windowed;
        // the screen the picture goes on, in pixels
        VideoMode screen = s.mode == DisplayMode::FullscreenExclusive && mode_sel > 0 ? m.modes[mode_sel - 1] : m.desktop;
        const int screen_w = int(screen.w * screen.density), screen_h = int(screen.h * screen.density);
        // windowed sizes are in screen coordinates; leave room for a title bar
        const int max_window = fit_scale(m.usable.w, m.usable.h - 40, s.aspect_8_7, s.widescreen);
        const int max_full = fit_scale(screen_w, screen_h, s.aspect_8_7, s.widescreen);
        s.window_scale = std::clamp(s.window_scale, 1, max_window);
        if (s.fullscreen_scale > max_full) s.fullscreen_scale = 0;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextUnformatted("Monster Party");
        ImGui::PopFont();
        ImGui::TextDisabled("Let's Go Again v%s", APP_VERSION);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float label_w = ImGui::CalcTextSize("Pixel shape").x + ImGui::GetStyle().WindowPadding.x + 16 * ui_scale;
        const float field_w = -FLT_MIN;   // fill to the right edge of the column

        // Two columns: video and sound on the left, the game's options on the right (drawn first);
        // the buttons go along the bottom.
        const ImGuiStyle& lst = ImGui::GetStyle();
        const float top_y = ImGui::GetCursorPosY();
        const float bottom_h = ImGui::GetFrameHeight() * 1.5f + lst.WindowPadding.y + lst.ItemSpacing.y * 2;
        const float child_h = ImGui::GetWindowHeight() - top_y - bottom_h;
        const float col_gap = 28 * ui_scale;
        const float col_w = (ImGui::GetContentRegionAvail().x - col_gap) * 0.5f;
        ImGui::SetCursorPos(ImVec2(lst.WindowPadding.x + col_w + col_gap, top_y));
        ImGui::BeginChild("##game_column", ImVec2(col_w, child_h), ImGuiChildFlags_NavFlattened);
        ImGui::SeparatorText("Game");

        // ---- which game: the US release, or the Japanese prototype if its ROM is in roms/
        row_label("Game", label_w);
        {
            bool proto = proto_ok && s.prototype;
            ImGui::BeginDisabled(!proto_ok);
            if (ImGui::Checkbox("Play the Japanese prototype", &proto)) s.prototype = proto;
            ImGui::EndDisabled();
            if (proto_ok)
                ImGui::SetItemTooltip("Parody World - Monster Party, the unreleased Japanese version:\n"
                                      "Japanese text, several different bosses and enemies.");
            else
                ImGui::SetItemTooltip("Put \"Parody World - Monster Party (Japan) (Proto).nes\" in the roms folder to enable this.\n\n%s",
                                      proto_why.c_str());
        }

        // ---- language (not in the original games): each game's own text, or both in one language
        row_label("Language", label_w);
        {
            Language lang = Language::Default;
            parse_language(s.language, lang);
            // (日本語 needs a font with those characters: one is merged in if the system has one)
            const char* ja_name = japanese_label_ok ? "日本語" : "Japanese";
            const char* names[] = {"Game Default", "English", ja_name};
            const char* tips[] = {
                "Each game in its own language: the US release in English,\nthe Japanese prototype in Japanese.",
                "Both games in English: the Japanese prototype's text is translated\n(story, speech bubbles, password and game over screens).\nThe title screen stays as it is.",
                "Both games in Japanese: the US release's text is translated\n(story, speech bubbles, password and game over screens),\nin the prototype's own font. The title screen stays as it is."};
            if (lang == Language::Japanese && !proto_ok) lang = Language::Default;
            int cur = int(lang);
            ImGui::SetNextItemWidth(field_w);
            if (ImGui::BeginCombo("##language", names[cur])) {
                for (int i = 0; i < 3; i++) {
                    const bool off = i == int(Language::Japanese) && !proto_ok;
                    ImGui::BeginDisabled(off);
                    if (ImGui::Selectable(names[i], cur == i)) s.language = language_setting(Language(i));
                    ImGui::EndDisabled();
                    if (off)
                        ImGui::SetItemTooltip("%s\n\nNeeds the Japanese prototype's ROM for its font: put\n\"Parody World - Monster Party (Japan) (Proto).nes\" in the roms folder.", tips[i]);
                    else ImGui::SetItemTooltip("%s", tips[i]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("%s", tips[cur]);
        }

        // ---- difficulty (not in the original game; Normal is the game as released)
        row_label("Difficulty", label_w);
        {
            static const char* names[] = {"Casual (easy)", "Normal", "Nasty (hard)"};
            static const char* tips[] = {
                "Enemies and bosses go down in about half the hits.\n"
                "Defeated enemies drop health and Bert capsules more often.",
                "The game as it was released.",
                "Enemies do double damage, bosses 1 more than normal,\n"
                "and both take about 1.5x the hits.\n"
                "Fewer health and Bert capsule drops, and a knocked-back shot only\n"
                "kills an enemy outright half the time (otherwise it does 3 damage)."};
            s.difficulty = std::clamp(s.difficulty, 0, 2);
            ImGui::SetNextItemWidth(field_w);
            if (ImGui::BeginCombo("##difficulty", names[s.difficulty])) {
                for (int i = 0; i < 3; i++) {
                    if (ImGui::Selectable(names[i], s.difficulty == i)) s.difficulty = i;
                    ImGui::SetItemTooltip("%s", tips[i]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("%s", tips[s.difficulty]);
        }

        // ---- leaderboards (not in the original game)
        row_label("Scores", label_w);
        {
            // Any trainer on = a cheating run, which is never logged: grey the checkbox out (the setting itself is kept,
            // so it's back as it was when the trainers are turned off). Same set as Trainers::any() in nes.h.
            const bool trainers_on = s.trainer_infinite_life || s.trainer_full_life || s.trainer_start_round_on ||
                                     s.trainer_super_jump || s.trainer_moon_jump || s.trainer_always_bert || s.trainer_mark_doors;
            ImGui::BeginDisabled(trainers_on);
            bool lb_shown = s.leaderboards && !trainers_on;
            if (ImGui::Checkbox("Leaderboards", &lb_shown)) s.leaderboards = lb_shown;
            ImGui::EndDisabled();
            if (trainers_on && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Not available while a trainer is on: games with trainers aren't logged.");
            else
                ImGui::SetItemTooltip("Log the time and score of a finished game (new game, no cheats), and\n"
                                      "let the credits go on to a result screen with your place.");
            ImGui::SameLine();
            if (ImGui::Button("View...")) ImGui::OpenPopup("Leaderboards");
            ImGui::SetItemTooltip("The fastest times and highest scores for each game version, difficulty and Remix setting.");
            leaderboards_popup(s, ui_scale);
        }

        // ---- Remix Mode (not in the original game)
        row_label("Remix", label_w);
        {
            ImGui::Checkbox("##remix", &s.remix);
            ImGui::SetItemTooltip("Remix Mode: each game, the bosses and the empty boss rooms are shuffled between\n"
                                  "the rounds (each round keeps its number of bosses), and each round gets a new\n"
                                  "set of enemy types from the other rounds (the same number, of the same kinds).\n"
                                  "Round 6's doors are shuffled too (it can always be beaten).\n"
                                  "The remix stays the same until you quit.");
            ImGui::SameLine();
            static char seed_buf[16];
            static bool seed_init = false;
            if (!seed_init) { std::snprintf(seed_buf, sizeof seed_buf, "%s", s.remix_seed.c_str()); seed_init = true; }
            ImGui::BeginDisabled(!s.remix);
            const float use_w = s.remix_last_seed.empty() ? 0 : ImGui::CalcTextSize("Replay 0000-0000").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(-FLT_MIN - use_w);
            if (ImGui::InputTextWithHint("##remix_seed", "Seed (empty = random)", seed_buf, sizeof seed_buf,
                                         ImGuiInputTextFlags_CharsUppercase)) s.remix_seed = seed_buf;
            ImGui::SetItemTooltip("Leave empty for a new random remix each time, or type a seed\n"
                                  "(8 hex digits, e.g. 1A2B-3C4D) to play that remix again.\n"
                                  "The seed is shown in the game window's title.");
            if (!s.remix_last_seed.empty()) {
                ImGui::SameLine();
                std::string lbl = "Replay " + s.remix_last_seed;
                if (ImGui::Button(lbl.c_str())) { s.remix_seed = s.remix_last_seed; std::snprintf(seed_buf, sizeof seed_buf, "%s", s.remix_seed.c_str()); }
                ImGui::SetItemTooltip("Put the last game's seed in the box, to play that remix again.");
            }
            ImGui::EndDisabled();
        }
        // ---- Bosses (with or without Remix)
        ImGui::Spacing();
        ImGui::SeparatorText("Bosses");
        {
            const bool proto = proto_ok && s.prototype;
            ImGui::BeginDisabled(!proto);
            bool totem = s.totem_pole && proto;
            if (ImGui::Checkbox("Include the unused Totem Pole boss", &totem)) s.totem_pole = totem;
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Japanese prototype only");
            ImGui::BeginDisabled(!proto || !s.totem_pole);
            bool attack = s.totem_attack && proto && s.totem_pole;
            if (ImGui::Checkbox("'Finished' Totem Pole boss", &attack)) s.totem_attack = attack;
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Adds some things to the unfinished boss to make it fit in better");
            ImGui::Checkbox("Fix Round 7 Key Steal bug", &s.fix_round7_key);
            ImGui::SetItemTooltip("Fixes the bug in Round 7 that softlocks you if you beat more than 2 bosses");
        }

        // ---- Trainers (not in the original game): cheats, like Game Genie codes
        ImGui::Spacing();
        ImGui::SeparatorText("Trainers");
        {
            const bool proto = proto_ok && s.prototype;
            const float col2 = ImGui::GetContentRegionAvail().x * 0.5f;   // (not indented: the checkboxes start at the left edge)
            ImGui::Checkbox("Infinite Life", &s.trainer_infinite_life);
            ImGui::SetItemTooltip("Enemies and bosses can still knock you back, but you never lose health.");
            ImGui::SameLine(col2);
            ImGui::Checkbox("Start with Full Life", &s.trainer_full_life);
            ImGui::SetItemTooltip("A new game, and a continue after Game Over, start with all the health\n"
                                  "you can have instead of the usual 3 bars.");
            if (ImGui::Checkbox("Super Jump", &s.trainer_super_jump) && s.trainer_super_jump) s.trainer_moon_jump = false;
            ImGui::SetItemTooltip(proto ? "Mark (and Baryuu) jump twice as high." : "Mark (and Bert) jump twice as high.");
            ImGui::SameLine(col2);
            if (ImGui::Checkbox("Moon Jump", &s.trainer_moon_jump) && s.trainer_moon_jump) s.trainer_super_jump = false;
            ImGui::SetItemTooltip("Mark jumps ridiculously high (about four times as high, as %s too),\n"
                                  "and floats back down with less gravity.", proto ? "Baryuu" : "Bert");
            ImGui::Checkbox(proto ? "Always play as Baryuu" : "Always play as Bert", &s.trainer_always_bert);
            ImGui::SetItemTooltip(proto ? "Start the game as Baryuu, and his time never runs out."
                                        : "Start the game as Bert, and his time never runs out.");
            ImGui::SameLine(col2);
            ImGui::Checkbox("Mark Round 6 Doors", &s.trainer_mark_doors);
            ImGui::SetItemTooltip("In Round 6's door maze, every door you've gone through is drawn in another\n"
                                  "colour, until you start a new game from the title (continues keep them).");
            ImGui::Checkbox("Start on Round", &s.trainer_start_round_on);
            ImGui::SetItemTooltip("Start a new game (START on the title screen) on this round.");
            ImGui::SameLine();
            s.trainer_start_round = std::clamp(s.trainer_start_round, 2, 8);
            // the box holds the number and its -/+ buttons, so it needs room for all three
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("88").x + lst.FramePadding.x * 2 + (ImGui::GetFrameHeight() + lst.ItemInnerSpacing.x) * 2 + 8 * ui_scale);
            if (ImGui::InputInt("##start_round", &s.trainer_start_round, 1, 1)) {
                s.trainer_start_round = std::clamp(s.trainer_start_round, 2, 8);
                s.trainer_start_round_on = true;   // changing the round turns the trainer on
            }
            ImGui::SetItemTooltip("Round 2 to 8.");
        }
        ImGui::EndChild();

        // ---- left column: video and sound
        ImGui::SetCursorPos(ImVec2(lst.WindowPadding.x, top_y));
        ImGui::BeginChild("##video_column", ImVec2(col_w, child_h), ImGuiChildFlags_NavFlattened);
        ImGui::SeparatorText("Video and sound");

        // ---- monitor
        row_label("Monitor", label_w);
        ImGui::SetNextItemWidth(field_w);
        if (ImGui::BeginCombo("##monitor", m.name.c_str())) {
            for (size_t i = 0; i < mons.size(); i++) {
                char lbl[160];
                std::snprintf(lbl, sizeof lbl, "%s  (%d x %d)", mons[i].name.c_str(),
                              int(mons[i].desktop.w * mons[i].desktop.density), int(mons[i].desktop.h * mons[i].desktop.density));
                if (ImGui::Selectable(lbl, int(i) == mon)) { mon = int(i); mode_sel = find_mode(mons[mon]); }
                if (int(i) == mon) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // ---- display mode
        row_label("Display", label_w);
        int dm = int(s.mode);
        ImGui::RadioButton("Windowed", &dm, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Fullscreen", &dm, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Exclusive", &dm, 2);
        ImGui::SetItemTooltip("Fullscreen that switches the monitor to the resolution chosen below.\n"
                              "\"Fullscreen\" keeps your desktop resolution and is usually smoother to switch.");
        s.mode = DisplayMode(dm);

        // ---- resolution (exclusive only)
        ImGui::BeginDisabled(s.mode != DisplayMode::FullscreenExclusive);
        row_label("Resolution", label_w);
        ImGui::SetNextItemWidth(field_w);
        std::string cur = mode_sel == 0 ? "Desktop  (" + mode_label(m.desktop) + ")" : mode_label(m.modes[mode_sel - 1]);
        if (ImGui::BeginCombo("##resolution", cur.c_str())) {
            std::string d = "Desktop  (" + mode_label(m.desktop) + ")";
            if (ImGui::Selectable(d.c_str(), mode_sel == 0)) mode_sel = 0;
            for (size_t i = 0; i < m.modes.size(); i++)
                if (ImGui::Selectable(mode_label(m.modes[i]).c_str(), mode_sel == int(i) + 1)) mode_sel = int(i) + 1;
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        // ---- scale
        row_label("Scale", label_w);
        ImGui::SetNextItemWidth(field_w);
        auto scale_label = [&](int n) {
            char b[64];
            std::snprintf(b, sizeof b, "%dx   (%d x %d)", n, picture_w(n, s.aspect_8_7, s.widescreen), picture_h(n));
            return std::string(b);
        };
        if (!full) {
            if (ImGui::BeginCombo("##scale", scale_label(s.window_scale).c_str())) {
                for (int n = 1; n <= max_window; n++)
                    if (ImGui::Selectable(scale_label(n).c_str(), n == s.window_scale)) s.window_scale = n;
                ImGui::EndCombo();
            }
        } else {
            ImGui::BeginDisabled(s.fill_screen);
            std::string auto_lbl = "Largest that fits   (" + std::to_string(max_full) + "x)";
            std::string preview = s.fullscreen_scale == 0 ? auto_lbl : scale_label(s.fullscreen_scale);
            if (ImGui::BeginCombo("##scale", preview.c_str())) {
                if (ImGui::Selectable(auto_lbl.c_str(), s.fullscreen_scale == 0)) s.fullscreen_scale = 0;
                for (int n = 1; n <= max_full; n++)
                    if (ImGui::Selectable(scale_label(n).c_str(), n == s.fullscreen_scale)) s.fullscreen_scale = n;
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }

        // ---- fill the screen
        row_label("Fit", label_w);
        ImGui::Checkbox("Fill the screen height", &s.fill_screen);
        ImGui::SetItemTooltip("Scales the picture as large as the window or screen allows, keeping its shape,\n"
                              "instead of the largest whole-number scale. Fewer black borders, but the pixels\n"
                              "are no longer all the same size: they're scaled up sharp, then smoothed down,\n"
                              "so edges between them can look slightly soft.");

        // ---- pixel shape
        row_label("Pixel shape", label_w);
        int ar = s.aspect_8_7 ? 1 : 0;
        ImGui::RadioButton("Square (1:1)", &ar, 0);
        ImGui::SameLine();
        ImGui::RadioButton("NES TV (8:7)", &ar, 1);
        ImGui::SetItemTooltip("Pixels a little wider than tall, as on a CRT TV.\n"
                              "The height stays a whole-number scale; the width is rounded, so some columns are 1 pixel wider.");
        s.aspect_8_7 = ar == 1;

        // ---- widescreen
        row_label("Widescreen", label_w);
        ImGui::Checkbox("16:9 (experimental)", &s.widescreen);
        ImGui::SetItemTooltip("Shows about %d more pixels of the room on each side.\n"
                              "Rooms are drawn from the game's map data; other screens have their edges extended.\n"
                              "Gameplay is unchanged, so enemies still appear and disappear where the\n"
                              "original screen's edges were, so while playing (outside boss rooms) the extra\n"
                              "room either side is a little darker.", wide_margin(s.aspect_8_7));

        // ---- 3D mode
        row_label("3D", label_w);
        ImGui::Checkbox("3D mode (experimental)", &s.mode3d);
        ImGui::SetItemTooltip("Draws the game in 3D with OpenGL: floors, walls and platforms become blocks, the\n"
                              "room's background is a wall further back that scrolls slower, and the sprites stay\n"
                              "flat in front. The game itself plays exactly the same.\n"
                              "Shows the 16:9 view if Widescreen is on, else just the original screen.\n"
                              "In game: move the mouse to turn the camera, the wheel zooms, right click puts\n"
                              "it back. Esc lets go of the mouse; click in the window to take it again.\n"
                              "A gamepad's right stick turns the camera too, and clicking it puts it back.\n"
                              "Shaders are off in 3D mode.");

        // ---- palette
        row_label("Palette", label_w);
        ImGui::SetNextItemWidth(field_w);
        {
            const PaletteFile* cur = nullptr;
            for (auto& p : palettes) if (p.setting == s.palette) cur = &p;
            std::string preview_label = s.palette.empty() ? "Default" : cur ? cur->name : mp_file_name(s.palette);
            if (ImGui::BeginCombo("##palette", preview_label.c_str())) {
                if (ImGui::IsWindowAppearing()) palettes = list_palettes();
                if (ImGui::Selectable("Default", s.palette.empty())) s.palette.clear();
                ImGui::SetItemTooltip("The built-in colours, generated from the NES's NTSC signal.");
                bool user_header = false;
                for (auto& p : palettes) {
                    if (p.user && !user_header) { ImGui::SeparatorText("Yours"); user_header = true; }
                    if (ImGui::Selectable(p.name.c_str(), p.setting == s.palette)) s.palette = p.setting;
                    ImGui::SetItemTooltip("%s", resolve_palette_path(p.setting).c_str());
                }
                ImGui::Separator();
                if (ImGui::Selectable("Open my palettes folder...")) {
                    // make the folder, with a copy of the default colours to start from if it's empty
                    const std::string dir = user_palette_dir();
                    SDL_CreateDirectory(dir.c_str());
                    bool any = false;
                    for (auto& p : palettes) any |= p.user;
                    if (!any) {
                        uint32_t def[64];
                        default_palette(def);
                        std::string err;
                        if (!save_palette(dir + "My palette.pal", def, err)) palette_error = err;
                    }
                    mp_open_folder(dir);
                    palettes = list_palettes();
                }
                ImGui::SetItemTooltip("%s\nPut .pal palette files here (JASC-PAL or binary). If it's empty, a copy\n"
                                      "of the default palette (My palette.pal) is made for you to edit.", user_palette_dir().c_str());
                ImGui::EndCombo();
            }
            // the 64 colours of the chosen palette
            if (preview_for != s.palette) {
                preview_for = s.palette;
                palette_error.clear();
                default_palette(preview);
                if (!s.palette.empty()) {
                    std::string err;
                    if (!load_palette(resolve_palette_path(s.palette), preview, nullptr, err)) { palette_error = err; default_palette(preview); }
                }
            }
            ImGui::SetCursorPosX(label_w);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float cw = (ImGui::GetContentRegionAvail().x) / 16.0f, ch = 7.0f * ui_scale;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int c = 0; c < 64; c++) {
                const uint32_t v = preview[c];
                const ImVec2 a(p0.x + (c & 15) * cw, p0.y + (c >> 4) * ch);
                dl->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 255));
            }
            ImGui::Dummy(ImVec2(cw * 16, ch * 4));
            if (!palette_error.empty()) {
                ImGui::SetCursorPosX(label_w);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                ImGui::TextWrapped("%s", palette_error.c_str());
                ImGui::PopStyleColor();
            }
        }

        // ---- sound on or off: off = no audio output at all
        row_label("Sound", label_w);
        ImGui::Checkbox("##sound", &s.sound);
        ImGui::SetItemTooltip("Off: the game makes no sound at all (music or effects).");

        // ---- music: the NES sound chip, or MIDI through a SoundFont (sound effects always use the chip)
        ImGui::BeginDisabled(!s.sound);
        row_label("Music", label_w);
        const float inst_w = ImGui::CalcTextSize("Instruments...").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - inst_w - ImGui::GetStyle().ItemSpacing.x);
        {
            std::string cur_sf = s.soundfont;
            if (cur_sf.empty() && !soundfonts.empty()) cur_sf = soundfonts[0].setting;
            const SoundFontFile* cur = nullptr;
            for (auto& f : soundfonts) if (f.setting == cur_sf) cur = &f;
            std::string label = s.music_off ? "No music" : !s.music_midi ? "NES sound chip"
                              : "MIDI: " + (cur ? cur->name : cur_sf.empty() ? std::string("(no SoundFont)") : mp_file_name(cur_sf));
            if (ImGui::BeginCombo("##music", label.c_str())) {
                if (ImGui::IsWindowAppearing()) soundfonts = list_soundfonts();
                if (ImGui::Selectable("No music", s.music_off)) s.music_off = true;
                ImGui::SetItemTooltip("No music at all. Sound effects still play as usual.");
                if (ImGui::Selectable("NES sound chip", !s.music_off && !s.music_midi)) { s.music_off = false; s.music_midi = false; }
                ImGui::SetItemTooltip("The game's music as the NES played it.");
                bool user_seen = false;
                for (auto& f : soundfonts) {
                    if (f.user && !user_seen) { ImGui::SeparatorText("Yours"); user_seen = true; }
                    std::string item = "MIDI: " + f.name;
                    if (ImGui::Selectable(item.c_str(), !s.music_off && s.music_midi && f.setting == cur_sf)) { s.music_off = false; s.music_midi = true; s.soundfont = f.setting; }
                    ImGui::SetItemTooltip("The music converted to MIDI as it plays, through this SoundFont.\n"
                                          "Sound effects still use the NES sound chip.\n%s", resolve_soundfont_path(f.setting).c_str());
                }
                ImGui::Separator();
                if (ImGui::Selectable("Open my SoundFonts folder...")) {
                    const std::string dir = user_soundfont_dir();
                    SDL_CreateDirectory(dir.c_str());
                    mp_open_folder(dir);
                    soundfonts = list_soundfonts();
                }
                ImGui::SetItemTooltip("%s\nPut .sf2 SoundFont files here; they show up in this list.", user_soundfont_dir().c_str());
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!s.music_midi || s.music_off);
            if (ImGui::Button("Instruments...")) ImGui::OpenPopup("MIDI instruments");
#ifdef LAUNCHER_SNAPSHOT_TEST   // test build only: show the instruments popup in the picture
            if (std::getenv("MP_TEST_INSTRUMENTS") && ImGui::GetFrameCount() == 2) ImGui::OpenPopup("MIDI instruments");
#endif
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Choose the MIDI instrument for each music channel.");
            instruments_popup(s, ui_scale);
        }
        ImGui::EndDisabled();

        // ---- shader (not in 3D mode)
        row_label("Shader", label_w);
        ImGui::SetNextItemWidth(field_w);
        ImGui::BeginDisabled(s.mode3d);
        {
            const BundledShader* cur_b = nullptr;
            for (auto* b : shaders) if (s.shader == b->path) cur_b = b;
            std::string custom = mp_file_name(s.shader);
            std::string preview = s.shader.empty() ? "None" : cur_b ? cur_b->label : custom;
            if (ImGui::BeginCombo("##shader", preview.c_str())) {
                if (ImGui::Selectable("None", s.shader.empty())) s.shader.clear();
                ImGui::SetItemTooltip("Plain sharp pixels, drawn with SDL's renderer (no OpenGL needed).");
                for (auto* b : shaders) {
                    if (ImGui::Selectable(b->label, cur_b == b)) s.shader = b->path;
                    ImGui::SetItemTooltip("%s", b->tip);
                }
                if (!s.shader.empty() && !cur_b) {
                    ImGui::Selectable(custom.c_str(), true);
                    ImGui::SetItemTooltip("%s", resolve_shader_path(s.shader).c_str());
                }
                ImGui::Separator();
                if (ImGui::Selectable("Other file...")) {
                    static const SDL_DialogFileFilter filters[] = {{"RetroArch GLSL shaders", "glslp;glsl"}};
                    SDL_ShowOpenFileDialog(on_file_picked, nullptr, window, filters, 1,
                                           shader_dir.empty() ? nullptr : shader_dir.c_str(), false);
                }
                ImGui::SetItemTooltip("Any RetroArch .glslp preset or .glsl shader, e.g. from libretro/glsl-shaders.");
                ImGui::EndCombo();
            }
            if (cur_b) ImGui::SetItemTooltip("%s", cur_b->tip);
            else if (shaders.empty() && s.shader.empty())
                ImGui::SetItemTooltip("No shader folder found next to the game or in the build; use \"Other file...\".");
        }
        ImGui::EndDisabled();
        if (s.mode3d) ImGui::SetItemTooltip("Shaders are off in 3D mode.");

        ImGui::EndChild();

        // ---- along the bottom:  Controls / Quit / START on the right
        const ImGuiStyle& st = ImGui::GetStyle();
        const float bw = 120 * ui_scale, bh = ImGui::GetFrameHeight() * 1.5f;
        const float bottom_y = ImGui::GetWindowHeight() - st.WindowPadding.y - bh;
        ImGui::SetCursorPos(ImVec2(st.WindowPadding.x, bottom_y - st.ItemSpacing.y));
        ImGui::Separator();
        ImGui::SetCursorPos(ImVec2(st.WindowPadding.x, bottom_y + (bh - ImGui::GetFrameHeight()) * 0.5f));

        ImGui::SameLine(0, 24 * ui_scale);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("In game: F11 or Alt+Enter toggles fullscreen.");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 3 - st.ItemSpacing.x * 2, bottom_y));
        if (ImGui::Button("Controls...", ImVec2(bw, bh))) { listen = Listen{}; ImGui::OpenPopup("Controls"); }
        ImGui::SetItemTooltip("Change the keys and gamepad buttons.");
#ifdef LAUNCHER_SNAPSHOT_TEST   // test build only: show the Controls popup in the picture
        if (std::getenv("MP_TEST_CONTROLS") && ImGui::GetFrameCount() == 2) ImGui::OpenPopup("Controls");
#endif
        controls_popup(s, listen, ui_scale);
        if (!ImGui::IsPopupOpen("Controls")) listen = Listen{};
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 2 - st.ItemSpacing.x, bottom_y));
        if (ImGui::Button("Quit", ImVec2(bw, bh))) done = true;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.18f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.26f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.92f, 0.34f, 0.42f, 1.0f));
        if (ImGui::Button("START", ImVec2(bw, bh))) start = done = true;
        ImGui::PopStyleColor(3);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);

        // Enter starts and Esc quits, unless a drop-down is open (then they work on the drop-down)
        const bool popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popup_open && !popup_was_open) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) start = done = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) done = true;
        }
        popup_was_open = popup_open;
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
#ifdef LAUNCHER_SNAPSHOT_TEST   // test build only: save a picture of the launcher and leave
        static int frames = 0;
        if (++frames == 4) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) { SDL_SaveBMP(shot, LAUNCHER_SNAPSHOT_TEST); SDL_DestroySurface(shot); }
            done = true;
        }
#endif
        SDL_RenderPresent(renderer);
    }

    s.display = mons[mon].name;
    if (s.mode == DisplayMode::FullscreenExclusive) {
        const Monitor& m = mons[mon];
        if (mode_sel > 0) { s.exclusive_w = m.modes[mode_sel - 1].w; s.exclusive_h = m.modes[mode_sel - 1].h; s.exclusive_hz = m.modes[mode_sel - 1].hz; }
        else { s.exclusive_w = s.exclusive_h = 0; s.exclusive_hz = 0; }
    }

    for (SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return start;
}
