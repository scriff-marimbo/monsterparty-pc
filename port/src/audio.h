// Monster Party native port: sound. The game's own sound engine (translated with the rest of the code)
// writes the APU registers; this is a small emulation of the NES APU (2A03) that turns those writes
// into samples. Monster Party only uses the two pulse channels, the triangle and the noise channel
// (the DMC is switched off at start-up and only its output level is ever written), and it never
// reads the APU back, so the DMC's sample playback and the frame IRQ are left out.
#pragma once
#include <cstdint>
#include <vector>

class NesAudio {
public:
    static constexpr long CPU_CLOCK = 1789773;   // NTSC
    static constexpr int SAMPLE_RATE = 44100;

    NesAudio() { reset(); }
    void write(uint16_t addr, uint8_t v);         // $4000-$4017 (not $4014 / $4016)
    // finish the current video frame; returns the new samples (about 735)
    std::vector<int16_t>& end_frame();         // (the MIDI music is mixed into these afterwards)
    void reset();                                 // power on / console reset: all channels silent

private:
    struct Envelope {                             // volume envelope (pulse and noise)
        bool start = false, loop = false, constant = false;
        uint8_t period = 0, divider = 0, decay = 0;
        void clock();
        uint8_t volume() const { return constant ? period : decay; }
    };
    struct Pulse {
        bool second = false;                      // pulse 2: the sweep negates with two's complement
        bool enabled = false;
        uint8_t duty = 0, step = 0, length = 0;
        bool halt = false;                        // also the envelope's loop flag
        Envelope env;
        uint16_t period = 0, timer = 0;
        bool sweep_on = false, sweep_negate = false, sweep_reload = false;
        uint8_t sweep_period = 0, sweep_shift = 0, sweep_div = 0;
        uint16_t target() const;
        bool muted() const { return period < 8 || target() > 0x7FF; }
        void clock_timer();                       // every other CPU cycle
        void clock_sweep();                       // half frame
        uint8_t output() const;
    };
    struct Triangle {
        bool enabled = false, control = false, linear_reload = false;
        uint8_t length = 0, linear = 0, linear_period = 0, step = 0;
        uint16_t period = 0, timer = 0;
        void clock_timer();                       // every CPU cycle
        void clock_linear();                      // quarter frame
        uint8_t output() const;
    };
    struct Noise {
        bool enabled = false, halt = false, mode = false;
        uint8_t length = 0;
        Envelope env;
        uint16_t period = 4, timer = 0, shift = 1;
        void clock_timer();                       // every other CPU cycle
        uint8_t output() const { return (shift & 1) || !length ? 0 : env.volume(); }
    };

    Pulse pulse1, pulse2;
    Triangle tri;
    Noise noise;
    uint8_t dmc_level = 0;                        // $4011: the DMC's output level, mixed in as a constant

    // frame sequencer: quarter frames clock envelopes and the triangle's linear counter,
    // half frames clock length counters and sweeps
    bool five_step = false;
    uint32_t seq_cycle = 0;
    void quarter_frame();
    void half_frame();

    // time and output
    uint32_t frame_cycle = 0;                     // CPU cycles run so far in this video frame
    uint32_t write_time = 0;                      // where the next register write lands in this frame
    bool odd_cycle = false;                       // pulse and noise timers tick on every other CPU cycle
    bool odd_frame = false;                       // NTSC frame = 29780.5 CPU cycles: alternate 29780 / 29781
    double mix = 0;                               // current mixer output (0 .. ~1)
    double sum = 0;                               // mixer output summed over the current sample's cycles
    double sample_pos = 0, cycles_per_sample = double(CPU_CLOCK) / SAMPLE_RATE;
    double hp_prev_in = 0, hp_prev_out = 0, lp_prev = 0, lp2_prev = 0;   // output filters
    std::vector<int16_t> samples;                 // this frame's output
    bool new_frame = true;                        // clear samples before adding the next frame's

    void run_until(uint32_t cycle);               // advance the APU to `cycle` in this frame
    void update_mix();
    void emit_sample(double v);
};
