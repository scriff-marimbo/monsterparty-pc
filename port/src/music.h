// Monster Party native port: the game's music as MIDI.
//
// The game's sound engine runs 9 logical channels: 0-3 play the music (one per APU channel: pulse 1,
// pulse 2, triangle, noise) and 4-8 the sound effects, which take over an APU channel while they
// play. Hooks in the translated sound engine (translate.py MUSIC_HOOKS) report what the four music
// channels are doing: each new note, the volume register, the period. Once per frame that becomes
// MIDI: notes, pitch bend (vibrato and slides) and expression (volume envelopes), with the noise
// channel as drums. The MIDI can go to a SoundFont synth (TinySoundFont), which then replaces the
// APU's music while the sound effects stay on the APU, and/or be recorded to a .mid file.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct tsf;

// General MIDI programs (0-based) for the three melodic channels; the noise channel uses drums
struct MidiPrograms { int pulse1 = 80, pulse2 = 80, triangle = 33; };

class MusicMidi {
public:
    ~MusicMidi();
    // Loads a SoundFont (.sf2) and starts playing the music through it. err says why not.
    bool load_soundfont(const std::string& path, std::string& err);
    bool playing() const { return synth != nullptr; }   // the synth replaces the APU's music
    bool listening() const { return synth || recording; }

    // from the sound engine (Cpu::music_*), channel 0-3
    void key_on(int ch) { voice[ch].key_on = true; }
    void volume(int ch, uint8_t reg0) { voice[ch].reg0 = reg0; }
    void period(int ch, uint8_t lo, uint8_t hi) { voice[ch].lo = lo; voice[ch].hi = hi; }

    // Once per video frame: turn the channels' state into MIDI events, then (if a SoundFont is
    // loaded) render the frame's music and mix it into samples.
    void end_frame(std::vector<int16_t>& samples, int sample_rate);
    void all_notes_off();                 // console reset

    // Recording: every MIDI event, timed by frame, written as a standard MIDI file (format 0).
    void start_recording() { recording = true; }
    bool save_recording(const std::string& path, std::string& err) const;

    MidiPrograms programs;
    float gain_db = -11;                  // SoundFont output level: about as loud as the NES music it replaces

private:
    struct Voice {
        uint8_t reg0 = 0, lo = 0, hi = 0;
        bool key_on = false;
        int note = -1;                    // MIDI note sounding, -1 = none
        int bend = 8192, expression = -1;
        int last_volume = 0;              // volume on the previous frame
    };
    Voice voice[4];
    tsf* synth = nullptr;
    bool recording = false;
    uint64_t frame = 0;
    struct Event { uint64_t frame; uint8_t b[3]; uint8_t len; };
    std::vector<Event> events;

    void send(uint8_t status, uint8_t d1, uint8_t d2, int len = 3);   // to the synth and the recording
    void setup_channels();
};

// SoundFont files: the bundled one(s) and the player's own (soundfonts/ in mp_config_dir(), e.g. ~/.config/monsterparty/soundfonts/)
struct SoundFontFile { std::string name, setting; bool user; };
std::string user_soundfont_dir();
std::string bundled_soundfont_dir();     // "soundfonts/" next to the executable, else the downloaded GeneralUser GS
std::vector<SoundFontFile> list_soundfonts();
std::string resolve_soundfont_path(const std::string& setting);   // a bare file name is a bundled one
