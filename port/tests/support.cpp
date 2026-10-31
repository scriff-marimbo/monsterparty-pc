#include "support.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include "ram_symbols.h"

namespace mptest {

std::string rom_path(Game g) { return std::string(MP_TEST_ROM_DIR) + "/" + rom_info(g).file; }

bool have_rom(Game g) {
    std::string err;
    return check_rom(rom_path(g), g, err);
}

std::vector<uint8_t> load_prg(Game g) {
    auto nes = std::make_unique<Nes>();
    std::string err;
    if (!nes->load(rom_path(g), g, err)) return {};
    return nes->prg;
}

Script start_game(uint64_t frames) {
    Script s;
    s.frames = frames;
    for (uint64_t f : {200, 500, 800}) s.presses.push_back({f, BTN_START});
    return s;
}

Script play_round1(uint64_t frames) {
    Script s = start_game(frames);
    for (uint64_t f = 1000; f < 2600; f += 8) s.presses.push_back({f, BTN_RIGHT});
    for (uint64_t f = 1100; f < 2600; f += 64) s.presses.push_back({f, uint8_t(BTN_RIGHT | BTN_B)});
    for (uint64_t f = 1300; f < 2600; f += 200) s.presses.push_back({f, uint8_t(BTN_RIGHT | BTN_A)});
    return s;
}

Script enter_first_boss_door() {
    // the same as main.cpp's --enter-boss-door: walk right, put the player in front of round 1's
    // first boss door and press Up, then A to get past the speech bubbles
    Script s = start_game(1800);
    for (uint64_t f = 1000; f <= 1342; f += 8) s.presses.push_back({f, BTN_RIGHT});
    for (uint64_t f : {1351, 1352}) { s.pokes.push_back({f, 0x3CB, 0x90}); s.pokes.push_back({f, 0x3D6, 0x8A}); }
    s.presses.push_back({1352, BTN_UP});
    for (uint64_t f = 1380; f <= 1700; f += 20) s.presses.push_back({f, BTN_A});
    return s;
}

namespace {
const Script* current;
std::vector<int16_t>* audio;
bool on_frame(Nes& nes) {
    if (audio && nes.audio_out) audio->insert(audio->end(), nes.audio_out->begin(), nes.audio_out->end());
    if (current->watch) current->watch(nes);
    if (current->act) current->act(nes);
    nes.buttons = 0;
    for (auto& p : current->pokes) if (nes.frame_count == p.frame) nes.cpu.ram[p.addr & 0x7FF] = p.value;
    for (auto& p : current->presses)
        if (nes.frame_count >= p.frame && nes.frame_count < p.frame + 8) nes.buttons |= p.buttons;
    return nes.frame_count < current->frames;
}
}  // namespace

Result run(Game g, const Script& script, const Options& opt) {
    Result r;
    r.nes = std::make_unique<Nes>();
    Nes& nes = *r.nes;
    std::string err;
    if (!nes.load(rom_path(g), g, err)) { std::fprintf(stderr, "load: %s\n", err.c_str()); return r; }
    // in the order main.cpp applies them
    apply_bosses(nes.prg, g, opt.totem_pole, opt.fix_round7);
    if (opt.remix) apply_remix(nes.prg, g, opt.remix_seed, true, nes.remix);
    if (opt.boss_door >= 0) debug_boss_door(nes.prg, g, uint8_t(opt.boss_door), nes.remix);
    if (opt.remix_runtime) enable_remix_runtime(nes.prg, g, nes.remix);
    if (opt.totem_finished && g == Game::Proto) { enable_remix_runtime(nes.prg, g, nes.remix); nes.remix.totem_attack = true; }
    if (opt.language != Language::Default) {
        const Game other = g == Game::Proto ? Game::US : Game::Proto;
        if (!apply_language(nes, opt.language, have_rom(other) ? rom_path(other) : std::string(), err))
            std::fprintf(stderr, "language: %s\n", err.c_str());
    }
    nes.trainers = opt.trainers;
    nes.debug_used = opt.boss_door >= 0 || !script.pokes.empty() || opt.ending;
    nes.debug_ending = opt.ending;
    nes.music_off = opt.music_off;
    nes.lb.enabled = !opt.leaderboards_path.empty();
    nes.lb.path = opt.leaderboards_path;
    nes.lb.category = LbCategory{g == Game::Proto, int(opt.difficulty), opt.remix};
    nes.reset();
    nes.difficulty = opt.difficulty;
    nes.wide_margin = opt.wide_margin;
    nes.wide_dim = opt.wide_dim;
    if (opt.mode3d) {
        r.scene = std::make_unique<Scene3D>();
        r.scene->wide = opt.wide_margin > 0;
        nes.scene3d = r.scene.get();
        if (!opt.wide_margin) nes.wide_margin = 16;
    }
    if (opt.record_midi) nes.music.start_recording();
    std::vector<int16_t> samples;
    current = &script;
    audio = opt.collect_audio ? &samples : nullptr;
    nes.on_frame = on_frame;
    nes.run();
    current = nullptr; audio = nullptr;
    std::copy(nes.cpu.ram, nes.cpu.ram + 0x800, r.ram);
    r.ram_hash = fnv1a(r.ram, 0x800);
    r.frames = nes.frame_count;
    r.is_cheating = nes.is_cheating;
    r.totem_obj = nes.remix.totem_obj;
    if (!samples.empty()) {
        double sum = 0;
        for (int16_t v : samples) sum += double(v) * v;
        r.audio_rms = std::sqrt(sum / samples.size());
    }
    return r;
}

uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

bool recording_golden() {
    const char* e = std::getenv("MP_RECORD_GOLDEN");
    return e && *e && *e != '0';
}

uint32_t golden(const std::string& name, uint32_t actual) {
    std::map<std::string, std::string> lines;
    std::vector<std::string> order;
    {
        std::ifstream in(MP_TEST_GOLDEN);
        std::string l;
        while (std::getline(in, l)) {
            const size_t eq = l.find(" = ");
            if (l.empty() || l[0] == '#' || eq == std::string::npos) { order.push_back(l); continue; }
            lines[l.substr(0, eq)] = l.substr(eq + 3);
            order.push_back(l.substr(0, eq));
        }
    }
    if (recording_golden()) {
        char v[16];
        std::snprintf(v, sizeof v, "0x%08X", actual);
        if (!lines.count(name)) order.push_back(name);
        lines[name] = v;
        std::ofstream out(MP_TEST_GOLDEN);
        for (auto& k : order) {
            if (lines.count(k)) out << k << " = " << lines[k] << "\n";
            else out << k << "\n";
        }
        return actual;
    }
    auto it = lines.find(name);
    if (it == lines.end()) return ~actual;   // not recorded yet: fails, showing the actual value
    return uint32_t(std::strtoul(it->second.c_str(), nullptr, 16));
}

}  // namespace mptest
