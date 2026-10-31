// Monster Party native port: NES APU emulation (pulse x2, triangle, noise), after the NESdev wiki's
// description of the 2A03. Timers run cycle by cycle; each output sample is the average of the
// mixer's output over the CPU cycles it covers, then goes through the console's output filters.
#include "audio.h"
#include <cmath>

namespace {
const uint8_t LENGTHS[32] = {10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                             12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
const uint8_t DUTY[4][8] = {{0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
                            {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
const uint16_t NOISE_PERIODS[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

// the 2A03's non-linear mixer, as lookup tables (NESdev "APU Mixer")
struct MixTables {
    double pulse[31], tnd[203];
    MixTables() {
        pulse[0] = 0;
        for (int i = 1; i < 31; i++) pulse[i] = 95.52 / (8128.0 / i + 100);
        tnd[0] = 0;
        for (int i = 1; i < 203; i++) tnd[i] = 163.67 / (24329.0 / i + 100);
    }
} const MIX;

// frame sequencer steps, in CPU cycles after a $4017 write
constexpr uint32_t SEQ_Q1 = 7457, SEQ_H1 = 14913, SEQ_Q3 = 22371, SEQ_4_END = 29829, SEQ_5_END = 37281;

// output filters: a high-pass at 37 Hz (a Famicom's; the NES's own 90 Hz one thins the triangle's
// bass lines noticeably) and a two-stage low-pass at 14 kHz against the harshness of plain sampling
constexpr double HP_HZ = 37, LP_HZ = 14000;
constexpr double GAIN = 32767 * 1.24;  // mixer output to 16-bit samples: about as loud as the port was with Nes_Apu
}

// ---------------------------------------------------------------- channels
void NesAudio::Envelope::clock() {
    if (start) { start = false; decay = 15; divider = period; return; }
    if (divider) { divider--; return; }
    divider = period;
    if (decay) decay--;
    else if (loop) decay = 15;
}

uint16_t NesAudio::Pulse::target() const {
    uint16_t change = period >> sweep_shift;
    if (!sweep_negate) return uint16_t(period + change);
    // pulse 1 subtracts one more (one's complement); never below 0
    int t = int(period) - change - (second ? 0 : 1);
    return uint16_t(t < 0 ? 0 : t);
}

void NesAudio::Pulse::clock_timer() {
    if (timer) { timer--; return; }
    timer = period;
    step = (step + 1) & 7;
}

void NesAudio::Pulse::clock_sweep() {
    if (!sweep_div && sweep_on && sweep_shift && !muted()) period = target();
    if (!sweep_div || sweep_reload) { sweep_div = sweep_period; sweep_reload = false; }
    else sweep_div--;
}

uint8_t NesAudio::Pulse::output() const {
    if (!length || muted() || !DUTY[duty][step]) return 0;
    return env.volume();
}

void NesAudio::Triangle::clock_timer() {
    if (timer) { timer--; return; }
    timer = period;
    // the sequencer only moves while both counters are running; periods below 2 are ultrasonic
    // (they'd only make a click), so the channel holds its level instead, as most emulators do
    if (length && linear && period >= 2) step = (step + 1) & 31;
}

void NesAudio::Triangle::clock_linear() {
    if (linear_reload) linear = linear_period;
    else if (linear) linear--;
    if (!control) linear_reload = false;
}

uint8_t NesAudio::Triangle::output() const {
    return step < 16 ? uint8_t(15 - step) : uint8_t(step - 16);
}

void NesAudio::Noise::clock_timer() {
    if (timer) { timer--; return; }
    timer = uint16_t(period / 2 - 1);      // the table is in CPU cycles; this timer ticks every other one
    uint16_t bit = (shift ^ (shift >> (mode ? 6 : 1))) & 1;
    shift = uint16_t((shift >> 1) | (bit << 14));
}

// ---------------------------------------------------------------- frame sequencer
void NesAudio::quarter_frame() {
    pulse1.env.clock(); pulse2.env.clock(); noise.env.clock();
    tri.clock_linear();
}

void NesAudio::half_frame() {
    if (pulse1.length && !pulse1.halt) pulse1.length--;
    if (pulse2.length && !pulse2.halt) pulse2.length--;
    if (tri.length && !tri.control) tri.length--;
    if (noise.length && !noise.halt) noise.length--;
    pulse1.clock_sweep(); pulse2.clock_sweep();
}

// ---------------------------------------------------------------- registers
void NesAudio::reset() {
    pulse1 = Pulse(); pulse2 = Pulse(); pulse2.second = true;
    tri = Triangle(); noise = Noise();
    dmc_level = 0;
    five_step = false; seq_cycle = 0;
    frame_cycle = write_time = 0;
    sum = sample_pos = 0;
    new_frame = true;
    update_mix();
    // start the filters settled at the idle level (the triangle rests at 15), so there's no thump
    hp_prev_in = mix; hp_prev_out = lp_prev = lp2_prev = 0;
}

void NesAudio::write(uint16_t addr, uint8_t v) {
    // The port doesn't count CPU cycles. The game's sound engine writes its registers in one burst
    // per frame (UpdateSound), so spreading the writes a few cycles apart is enough.
    run_until(write_time);
    if (write_time < 29000) write_time += 8;

    if (addr <= 0x4007) {
        Pulse& p = addr < 0x4004 ? pulse1 : pulse2;
        switch (addr & 3) {
        case 0:
            p.duty = v >> 6; p.halt = p.env.loop = v & 0x20;
            p.env.constant = v & 0x10; p.env.period = v & 0x0F;
            break;
        case 1:
            p.sweep_on = v & 0x80; p.sweep_period = (v >> 4) & 7;
            p.sweep_negate = v & 0x08; p.sweep_shift = v & 7; p.sweep_reload = true;
            break;
        case 2: p.period = uint16_t((p.period & 0x700) | v); break;
        case 3:
            p.period = uint16_t((p.period & 0xFF) | (v & 7) << 8);
            if (p.enabled) p.length = LENGTHS[v >> 3];
            p.step = 0; p.env.start = true;
            break;
        }
    } else switch (addr) {
    case 0x4008: tri.control = v & 0x80; tri.linear_period = v & 0x7F; break;
    case 0x400A: tri.period = uint16_t((tri.period & 0x700) | v); break;
    case 0x400B:
        tri.period = uint16_t((tri.period & 0xFF) | (v & 7) << 8);
        if (tri.enabled) tri.length = LENGTHS[v >> 3];
        tri.linear_reload = true;
        break;
    case 0x400C:
        noise.halt = noise.env.loop = v & 0x20;
        noise.env.constant = v & 0x10; noise.env.period = v & 0x0F;
        break;
    case 0x400E: noise.mode = v & 0x80; noise.period = NOISE_PERIODS[v & 0x0F]; break;
    case 0x400F:
        if (noise.enabled) noise.length = LENGTHS[v >> 3];
        noise.env.start = true;
        break;
    case 0x4011: dmc_level = v & 0x7F; break;
    case 0x4015:
        pulse1.enabled = v & 1; pulse2.enabled = v & 2; tri.enabled = v & 4; noise.enabled = v & 8;
        if (!pulse1.enabled) pulse1.length = 0;
        if (!pulse2.enabled) pulse2.length = 0;
        if (!tri.enabled) tri.length = 0;
        if (!noise.enabled) noise.length = 0;
        break;
    case 0x4017:
        // restart the sequence; in 5-step mode the write itself clocks a quarter and a half frame.
        // (The game writes $C0 here every frame, from UpdateSound.)
        five_step = v & 0x80; seq_cycle = 0;
        if (five_step) { quarter_frame(); half_frame(); }
        break;
    }
    update_mix();
}

// ---------------------------------------------------------------- running and output
void NesAudio::update_mix() {
    int p = pulse1.output() + pulse2.output();
    int tnd = 3 * tri.output() + 2 * noise.output() + dmc_level;
    mix = MIX.pulse[p] + MIX.tnd[tnd];
}

void NesAudio::emit_sample(double v) {
    const double dt = 1.0 / SAMPLE_RATE;
    const double hp_rc = 1 / (2 * M_PI * HP_HZ), lp_rc = 1 / (2 * M_PI * LP_HZ);
    const double hp_a = hp_rc / (hp_rc + dt), lp_a = dt / (lp_rc + dt);
    double hp = hp_a * (hp_prev_out + v - hp_prev_in);
    hp_prev_in = v; hp_prev_out = hp;
    lp_prev += lp_a * (hp - lp_prev);
    lp2_prev += lp_a * (lp_prev - lp2_prev);
    double s = lp2_prev * GAIN;
    samples.push_back(int16_t(s > 32767 ? 32767 : s < -32768 ? -32768 : s));
}

void NesAudio::run_until(uint32_t cycle) {
    if (new_frame) { samples.clear(); new_frame = false; }   // the last frame's samples have been used
    for (; frame_cycle < cycle; frame_cycle++) {
        // frame sequencer
        seq_cycle++;
        if (seq_cycle == SEQ_Q1 || seq_cycle == SEQ_Q3) quarter_frame();
        else if (seq_cycle == SEQ_H1) { quarter_frame(); half_frame(); }
        else if (!five_step && seq_cycle == SEQ_4_END) { quarter_frame(); half_frame(); seq_cycle = 0; }
        else if (five_step && seq_cycle == SEQ_5_END) { quarter_frame(); half_frame(); seq_cycle = 0; }
        // timers
        tri.clock_timer();
        if (odd_cycle) { pulse1.clock_timer(); pulse2.clock_timer(); noise.clock_timer(); }
        odd_cycle = !odd_cycle;
        update_mix();
        // one output sample per ~40.6 CPU cycles: the average over those cycles
        sum += mix;
        sample_pos += 1;
        if (sample_pos >= cycles_per_sample) {
            // split the cycle that crosses the boundary between this sample and the next
            double over = sample_pos - cycles_per_sample;
            emit_sample((sum - mix * over) / cycles_per_sample);
            sum = mix * over;
            sample_pos = over;
        }
    }
}

std::vector<int16_t>& NesAudio::end_frame() {
    run_until(odd_frame ? 29781 : 29780);    // samples now holds the whole frame's output
    odd_frame = !odd_frame;
    frame_cycle = write_time = 0;
    new_frame = true;
    return samples;
}
