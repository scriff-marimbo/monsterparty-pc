// Monster Party native port: the music as MIDI (see music.h).
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "music.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr double CPU_CLOCK = 1789773;
constexpr int DRUM_CHANNEL = 9;
constexpr int MIDI_CHANNEL[4] = {0, 1, 2, DRUM_CHANNEL};
constexpr float BEND_RANGE = 2;          // semitones either way (the General MIDI default)

// noise period index (low = high pitched hiss) -> General MIDI drum
int drum_for(uint8_t noise_reg) {
    int index = noise_reg & 15;
    if (index <= 4) return 42;           // closed hi-hat
    if (index <= 8) return 38;           // snare
    return 36;                           // bass drum
}

int velocity_for(int volume) { return std::clamp(40 + volume * 5, 1, 127); }
}

MusicMidi::~MusicMidi() { if (synth) tsf_close(synth); }

bool MusicMidi::load_soundfont(const std::string& path, std::string& err) {
    // tsf_load_filename would use fopen, which can't open non-ASCII paths on Windows
    FILE* file = mp_fopen(path, "rb");
    tsf* f = nullptr;
    if (file) {
        tsf_stream stream = {file,
            [](void* d, void* p, unsigned int n) { return int(std::fread(p, 1, n, static_cast<FILE*>(d))); },
            [](void* d, unsigned int n) { return std::fseek(static_cast<FILE*>(d), long(n), SEEK_CUR) == 0 ? 1 : 0; }};
        f = tsf_load(&stream);
        std::fclose(file);
    }
    if (!f) { err = "can't load the SoundFont " + path; return false; }
    if (synth) tsf_close(synth);
    synth = f;
    tsf_set_max_voices(synth, 64);
    setup_channels();
    return true;
}

void MusicMidi::setup_channels() {
    if (!synth) return;
    tsf_set_output(synth, TSF_MONO, 44100, gain_db);
    const int prog[3] = {programs.pulse1, programs.pulse2, programs.triangle};
    for (int i = 0; i < 3; i++) {
        tsf_channel_set_presetnumber(synth, MIDI_CHANNEL[i], prog[i]);
        tsf_channel_set_pitchrange(synth, MIDI_CHANNEL[i], BEND_RANGE);
    }
    tsf_channel_set_presetnumber(synth, DRUM_CHANNEL, 0, 1);
}

void MusicMidi::send(uint8_t status, uint8_t d1, uint8_t d2, int len) {
    if (recording) events.push_back({frame, {status, d1, d2}, uint8_t(len)});
    if (!synth) return;
    const int ch = status & 15;
    switch (status & 0xF0) {
    case 0x90: tsf_channel_note_on(synth, ch, d1, d2 / 127.0f); break;
    case 0x80: tsf_channel_note_off(synth, ch, d1); break;
    case 0xB0: tsf_channel_midi_control(synth, ch, d1, d2); break;
    case 0xE0: tsf_channel_set_pitchwheel(synth, ch, d1 | d2 << 7); break;
    }
}

void MusicMidi::end_frame(std::vector<int16_t>& samples, int sample_rate) {
    if (recording && frame == 0) {
        const int prog[3] = {programs.pulse1, programs.pulse2, programs.triangle};
        for (int i = 0; i < 3; i++) events.push_back({0, {uint8_t(0xC0 | MIDI_CHANNEL[i]), uint8_t(prog[i]), 0}, 2});
    }
    for (int ch = 0; ch < 4; ch++) {
        Voice& v = voice[ch];
        const int mc = MIDI_CHANNEL[ch];
        const bool tri = ch == 2;
        // pulse and noise: constant volume in the low 4 bits; triangle: on while its linear counter value is set
        const int vol = tri ? ((v.reg0 & 0x7F) ? 12 : 0) : (v.reg0 & 15);
        if (ch == 3) {
            if (v.note >= 0) { send(uint8_t(0x80 | mc), uint8_t(v.note), 0); v.note = -1; }
            if (v.key_on && vol) { v.note = drum_for(v.lo); send(uint8_t(0x90 | mc), uint8_t(v.note), uint8_t(velocity_for(vol))); }
            v.key_on = false;
            continue;
        }
        const int period = v.lo | (v.hi & 7) << 8;
        const bool audible = vol > 0 && period >= (tri ? 2 : 8);
        if (!audible) {
            if (v.note >= 0) { send(uint8_t(0x80 | mc), uint8_t(v.note), 0); v.note = -1; }
            v.key_on = false;
            v.last_volume = 0;
            continue;
        }
        const double freq = CPU_CLOCK / ((tri ? 32.0 : 16.0) * (period + 1));
        const double pitch = 69 + 12 * std::log2(freq / 440);
        // (Re)start the note when it's new, when the sound data starts a different pitch, or when
        // the pitch jumps too far to bend. The sound data often re-triggers the same pitch just to
        // change its volume (an accent, or a soft-then-loud swell): that stays one MIDI note with
        // an expression change, unless the old note had faded right down (a repeated note).
        bool restart = v.note < 0 || std::fabs(pitch - v.note) > BEND_RANGE - 0.5;
        if (v.key_on && !restart)
            restart = std::lround(pitch) != v.note || v.last_volume * 3 <= vol;
        if (restart) {
            if (v.note >= 0) send(uint8_t(0x80 | mc), uint8_t(v.note), 0);
            v.note = std::clamp(int(std::lround(pitch)), 0, 127);
            v.bend = -1; v.expression = -1;
            send(uint8_t(0x90 | mc), uint8_t(v.note), uint8_t(velocity_for(vol)));
        }
        const int bend = std::clamp(int(std::lround(8192 + (pitch - v.note) / BEND_RANGE * 8192)), 0, 16383);
        if (bend != v.bend) { v.bend = bend; send(uint8_t(0xE0 | mc), uint8_t(bend & 0x7F), uint8_t(bend >> 7)); }
        const int expr = tri ? 127 : vol * 127 / 15;
        if (expr != v.expression) { v.expression = expr; send(uint8_t(0xB0 | mc), 11, uint8_t(expr)); }
        v.key_on = false;
        v.last_volume = vol;
    }
    frame++;
    if (synth && !samples.empty()) {
        if (sample_rate != 44100) tsf_set_output(synth, TSF_MONO, sample_rate, gain_db);
        tsf_render_short(synth, samples.data(), int(samples.size()), 1);   // mixed into the APU's samples
    }
}

void MusicMidi::all_notes_off() {
    for (int ch = 0; ch < 4; ch++) {
        if (voice[ch].note >= 0) send(uint8_t(0x80 | MIDI_CHANNEL[ch]), uint8_t(voice[ch].note), 0);
        voice[ch] = Voice();
    }
}

bool MusicMidi::save_recording(const std::string& path, std::string& err) const {
    // 480 ticks per quarter note at a tempo where one video frame (1/60.0988 s) is exactly 16 ticks
    std::vector<uint8_t> trk;
    auto var = [&](uint32_t v) {
        uint8_t b[4]; int n = 0;
        do { b[n++] = v & 0x7F; v >>= 7; } while (v);
        while (n--) trk.push_back(uint8_t(b[n] | (n ? 0x80 : 0)));
    };
    const uint32_t tempo = 499178;   // microseconds per quarter note = 30 frames
    var(0); trk.insert(trk.end(), {0xFF, 0x51, 3, uint8_t(tempo >> 16), uint8_t(tempo >> 8), uint8_t(tempo)});
    uint64_t last = 0;
    for (const Event& e : events) {
        var(uint32_t((e.frame - last) * 16)); last = e.frame;
        trk.insert(trk.end(), e.b, e.b + e.len);
    }
    var(0); trk.insert(trk.end(), {0xFF, 0x2F, 0});
    FILE* f = mp_fopen(path, "wb");
    if (!f) { err = "can't write " + path; return false; }
    const uint8_t head[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 480 >> 8, 480 & 0xFF, 'M', 'T', 'r', 'k',
                            uint8_t(trk.size() >> 24), uint8_t(trk.size() >> 16), uint8_t(trk.size() >> 8), uint8_t(trk.size())};
    bool ok = std::fwrite(head, 1, sizeof head, f) == sizeof head && std::fwrite(trk.data(), 1, trk.size(), f) == trk.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) err = "can't write " + path;
    return ok;
}

// ---------------------------------------------------------------- SoundFont files
static bool dir_exists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static bool has_ext(const std::string& f, const char* ext) {
    size_t n = std::strlen(ext);
    if (f.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(static_cast<unsigned char>(f[f.size() - n + i])) != ext[i]) return false;
    return true;
}

std::string user_soundfont_dir() {
    return mp_config_dir() + "soundfonts/";
}

std::string bundled_soundfont_dir() {
    if (const char* base = SDL_GetBasePath()) {
        std::string d = std::string(base) + "soundfonts/";
        if (dir_exists(d)) return d;
    }
#ifdef MP_SOUNDFONT_DIR
    if (dir_exists(MP_SOUNDFONT_DIR)) return std::string(MP_SOUNDFONT_DIR) + "/";
#endif
    return "";
}

std::string resolve_soundfont_path(const std::string& setting) {
    if (setting.empty() || mp_is_absolute(setting)) return setting;
    return bundled_soundfont_dir() + setting;
}

static void scan(const std::string& dir, bool user, std::vector<SoundFontFile>& out) {
    if (dir.empty() || !dir_exists(dir)) return;
    int n = 0;
    char** names = SDL_GlobDirectory(dir.c_str(), nullptr, 0, &n);
    std::vector<SoundFontFile> found;
    for (int i = 0; names && i < n; i++) {
        std::string file = names[i];
        if (!has_ext(file, ".sf2")) continue;
        found.push_back({file.substr(0, file.size() - 4), user ? dir + file : file, user});
    }
    SDL_free(names);
    std::sort(found.begin(), found.end(), [](const SoundFontFile& a, const SoundFontFile& b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
}

std::vector<SoundFontFile> list_soundfonts() {
    std::vector<SoundFontFile> out;
    scan(bundled_soundfont_dir(), false, out);
    scan(user_soundfont_dir(), true, out);
    return out;
}
