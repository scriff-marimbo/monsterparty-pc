// Monster Party native port: 6502 CPU state and the memory map seen by the translated code.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

struct Nes;   // PPU, mapper, controllers (nes.h)

#define R(sym) ram[sym]

struct Cpu {
    uint8_t A = 0, X = 0, Y = 0, SP = 0xFD;
    bool N = false, V = false, D = false, I = true, Z = false, C = false;
    uint8_t ram[0x800] = {};
    Nes* nes = nullptr;
    uint8_t prg_bank = 0;           // bank mapped at $8000 (kept in sync by the mapper)
    // CPU cycles since the last NMI, counted by the translated code (base cycles per instruction, +1 for a taken
    // branch; no page-crossing penalties). Only used to time the end-of-frame UpdateRandom spin (frame_hook).
    uint32_t cyc = 0;
    uint16_t rng_steps = 0;         // UpdateRandom calls in the last frame's spin (for tests)
    void update_random();           // UpdateRandom ($C66E / JP $C665), for the spin's extra rounds

    // ---- flags and ALU
    inline void nz(uint8_t v) { N = v & 0x80; Z = v == 0; }
    inline void adc(uint8_t v) {
        unsigned s = A + v + (C ? 1 : 0);
        V = (~(A ^ v) & (A ^ s) & 0x80) != 0;
        C = s > 0xFF; A = uint8_t(s); nz(A);
    }
    inline void cmp(uint8_t r, uint8_t v) { C = r >= v; nz(uint8_t(r - v)); }
    inline void bit(uint8_t v) { N = v & 0x80; V = v & 0x40; Z = (A & v) == 0; }
    inline uint8_t asl(uint8_t v) { C = v & 0x80; v <<= 1; nz(v); return v; }
    inline uint8_t lsr(uint8_t v) { C = v & 1; v >>= 1; nz(v); return v; }
    inline uint8_t rol(uint8_t v) { bool c = C; C = v & 0x80; v = uint8_t(v << 1 | (c ? 1 : 0)); nz(v); return v; }
    inline uint8_t ror(uint8_t v) { bool c = C; C = v & 1; v = uint8_t(v >> 1 | (c ? 0x80 : 0)); nz(v); return v; }
    inline uint8_t inc(uint8_t v) { v++; nz(v); return v; }
    inline uint8_t dec(uint8_t v) { v--; nz(v); return v; }

    // ---- stack (real 6502 stack in RAM $0100-$01FF: the game depends on it)
    inline void push(uint8_t v) { ram[0x100 + SP] = v; SP--; }
    inline uint8_t pull() { SP++; return ram[0x100 + SP]; }
    inline void push16(uint16_t v) { push(v >> 8); push(v & 0xFF); }
    inline uint16_t pull16() { uint8_t lo = pull(); return uint16_t(lo | pull() << 8); }
    inline uint8_t getP(bool brk) const {
        return uint8_t((N << 7) | (V << 6) | 0x20 | (brk << 4) | (D << 3) | (I << 2) | (Z << 1) | C);
    }
    inline void setP(uint8_t p) { N = p & 0x80; V = p & 0x40; D = p & 8; I = p & 4; Z = p & 2; C = p & 1; }
    inline uint16_t zp16(uint8_t a) { return uint16_t(ram[a] | ram[uint8_t(a + 1)] << 8); }

    // ---- memory map (nes.cpp)
    uint8_t rd(uint16_t addr);
    void wr(uint16_t addr, uint8_t v);
    uint16_t rd16(uint16_t addr) { return uint16_t(rd(addr) | rd(addr + 1) << 8); }

    // ---- control flow helpers
    inline bool in_bank(uint16_t pc, int bank) const {
        return bank == 7 ? pc >= 0xC000 : (pc >= 0x8000 && pc < 0xC000 && prg_bank == bank);
    }
    uint16_t unknown_pc(uint16_t pc, int bank) {
        std::fprintf(stderr, "fatal: no translated code for $%04X (bank %d, PRG bank at $8000 = %d)\n", pc, bank, prg_bank);
        std::exit(2);
    }

    // ---- hooks used by the translated code (nes.cpp)
    uint16_t frame_hook(uint16_t resume_pc);   // $C06D: end of frame, then enter the NMI handler
    void sprite0_split();                      // $C03F: the status-bar split point of the frame
    void ram191_trampoline();                  // $CF07: jmp $0191 (STA $40xx built in RAM)
    void wide_sprite_piece();                  // $C597: a sprite piece's 16-bit X, for the widescreen renderer
    // difficulty (nes.cpp): called just before the instructions listed in translate.py's DIFF_HOOKS
    void diff_player_damage_begin();           // the player is about to lose health
    void diff_player_damage_end();             // health has been lowered (not yet clamped at 0)
    uint8_t diff_enemy_hp(uint8_t hp);         // a normal enemy's starting HP
    uint8_t diff_boss_hp(uint8_t hp);          // a boss task's starting HP
    void diff_enemy_hit();                     // a normal enemy is about to take the damage in $10
    void diff_drop();                          // a dead enemy is about to turn into its drop
    // new boss behaviour (remix.cpp): translate.py BOSS_HOOKS
    bool totem_fire();                         // prototype, Totem Pole's handler: true = fire a bubble now (A/X/Y set up)
    // trainers (nes.cpp): translate.py TRAINER_HOOKS
    uint8_t trainer_start_health(uint8_t a);   // A = the health a new game / continue starts with
    void trainer_title_done();                 // the title screen ends: Y = 3 new game, 2 password screen
    void trainer_jump_step();                  // the player's jump is about to move to its next table step
    uint8_t trainer_jump(uint8_t a);           // A = this frame's jump movement (pixels up, $80 = end)
    void trainer_door_entered();               // TryEnterDoor found the door the player entered: Y = its entry x 2
    // leaderboards (nes.cpp): translate.py LB_HOOKS
    void lb_round_start();                     // ROUND n START is over (A or Start pressed)
    void lb_boss_reward();                     // a boss was just beaten (the final one stops the clock)
    void lb_track_score();                     // every frame: the run's best score so far
    void lb_final_boss();                      // the final boss is beaten: log the result
    bool lb_credits_idle();                    // the credits' last page is up; true = a button was pressed, go to the result screen
    void lb_screen_begin();                    // the GAME OVER screen's code starts (draws the result screen's text if that's what this is)
    void lb_screen_music();                    // ... its PlaySoundRange: the password screen's music instead of the game over tune
    bool lb_screen_frame();                    // ... its menu loop: true = the result screen, just wait
    // music as MIDI (nes.cpp, music.h): called from the sound engine (translate.py MUSIC_HOOKS)
    void music_key_on();                       // X = channel: a new note
    void music_volume();                       // X = channel: register 0 value in RAM $1E
    void music_period();                       // X = channel: period A (low) / Y (high)
    // Remix Mode, mixing rounds (remix.cpp): translate.py REMIX_HOOKS
    void remix_task_begin();                   // the scheduler is about to run task slot X
    void remix_task_end();                     // ... and has come back for the next one
    void remix_task_started();                 // StartTask is setting up slot Y
    uint8_t remix_draw_round(uint8_t a);       // DrawObject: A = RoundNumber, Y = object
    void remix_draw_untagged();                // DrawMetasprite (cutscenes etc.): not an object
    uint8_t remix_arena_round(uint8_t a);      // A = RoundNumber for RoundNumber * 4 + BossIdInRound
    // the Language setting (language.cpp): translate.py LANG_HOOKS
    void lang_bubble_start();                  // a speech bubble starts: A = RoundNumber*4 + BossIdInRound
    void lang_bubble_page();                   // a bubble page was copied to RAM $055C (rewrite its text)
};

// the translated code: gen/ (Monster Party, USA) and gen_jp/ (Parody World, the Japanese prototype)
uint16_t run_bank4(Cpu& c, uint16_t pc);
uint16_t run_bank5(Cpu& c, uint16_t pc);
uint16_t run_bank7(Cpu& c, uint16_t pc);
namespace jp {
uint16_t run_bank4(Cpu& c, uint16_t pc);
uint16_t run_bank5(Cpu& c, uint16_t pc);
uint16_t run_bank7(Cpu& c, uint16_t pc);
}
