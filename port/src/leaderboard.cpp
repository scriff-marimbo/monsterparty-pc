#include "leaderboard.h"
#include "platform.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------- categories
static const char* const kDifficultyKey[3] = {"casual", "normal", "nasty"};
static const char* const kDifficultyName[3] = {"Casual", "Normal", "Nasty"};

static int clamp_difficulty(int d) { return d < 0 ? 0 : d > 2 ? 2 : d; }

std::string LbCategory::key() const {
    return std::string(proto ? "jp" : "us") + "." + kDifficultyKey[clamp_difficulty(difficulty)] + "." + (remix ? "1" : "0");
}

std::string LbCategory::title() const {
    return std::string(proto ? "JP" : "US") + " - " + kDifficultyName[clamp_difficulty(difficulty)] + (remix ? " (Remix)" : "");
}

std::string LbCategory::screen_text() const {
    std::string s = std::string(proto ? "JP" : "US") + " - ";
    for (const char* p = kDifficultyName[clamp_difficulty(difficulty)]; *p; p++) s += char(std::toupper(static_cast<unsigned char>(*p)));
    if (remix) s += " - REMIX";
    return s;
}

// ---------------------------------------------------------------- the lists
static const LbBoard kEmptyBoard;

const LbBoard& Leaderboards::get(const LbCategory& c) const {
    auto it = boards_.find(c.key());
    return it == boards_.end() ? kEmptyBoard : it->second;
}

// Where e goes in a list kept in order by `better`: after every entry that isn't worse than it.
template <class Better>
static int place_in(std::vector<LbEntry>& list, const LbEntry& e, Better better) {
    size_t pos = 0;
    while (pos < list.size() && !better(e, list[pos])) pos++;
    if (pos >= size_t(LB_MAX)) return 0;
    list.insert(list.begin() + pos, e);
    if (list.size() > size_t(LB_MAX)) list.resize(LB_MAX);
    return int(pos) + 1;
}

LbPlace Leaderboards::add(const LbCategory& c, const LbEntry& e) {
    LbBoard& b = boards_[c.key()];
    LbPlace p;
    p.time = place_in(b.by_time, e, [](const LbEntry& a, const LbEntry& o) { return a.frames < o.frames; });
    p.score = place_in(b.by_score, e, [](const LbEntry& a, const LbEntry& o) { return a.score > o.score; });
    return p;
}

// ---------------------------------------------------------------- the file
bool Leaderboards::load(const std::string& path) {
    boards_.clear();
    FILE* f = mp_fopen(path, "r");
    if (!f) return true;   // no file yet
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char kind[16], key[64];
        unsigned frames = 0, score = 0;
        long long when = 0;
        if (line[0] == '#' || std::sscanf(line, "%15s %63s %u %u %lld", kind, key, &frames, &score, &when) < 4) continue;
        LbBoard& b = boards_[key];
        LbEntry e{frames, score, when};
        if (!std::strcmp(kind, "time")) b.by_time.push_back(e);
        else if (!std::strcmp(kind, "score")) b.by_score.push_back(e);
    }
    std::fclose(f);
    for (auto& [key, b] : boards_) {
        std::stable_sort(b.by_time.begin(), b.by_time.end(), [](const LbEntry& a, const LbEntry& o) { return a.frames < o.frames; });
        std::stable_sort(b.by_score.begin(), b.by_score.end(), [](const LbEntry& a, const LbEntry& o) { return a.score > o.score; });
        if (b.by_time.size() > size_t(LB_MAX)) b.by_time.resize(LB_MAX);
        if (b.by_score.size() > size_t(LB_MAX)) b.by_score.resize(LB_MAX);
    }
    return true;
}

bool Leaderboards::save(const std::string& path) const {
    const std::string tmp = path + ".tmp";
    FILE* f = mp_fopen(tmp, "w");
    if (!f) return false;
    std::fprintf(f, "# Monster Party port leaderboards (written by the game)\n"
                    "# time|score  ROM.difficulty.remix  frames  score(points)  unix-time\n");
    for (const auto& [key, b] : boards_) {
        for (const LbEntry& e : b.by_time) std::fprintf(f, "time %s %u %u %lld\n", key.c_str(), e.frames, e.score, static_cast<long long>(e.when));
        for (const LbEntry& e : b.by_score) std::fprintf(f, "score %s %u %u %lld\n", key.c_str(), e.frames, e.score, static_cast<long long>(e.when));
    }
    const bool ok = std::fclose(f) == 0;
    if (!ok || !mp_rename(tmp, path)) { mp_remove(tmp); return false; }
    return true;
}

LbPlace lb_log(const std::string& path, const LbCategory& c, const LbEntry& e, bool* ok) {
    Leaderboards all;
    if (!path.empty()) all.load(path);
    LbPlace p = all.add(c, e);
    const bool saved = path.empty() || all.save(path);
    if (ok) *ok = saved;
    return p;
}

// ---------------------------------------------------------------- formatting
double lb_seconds(uint32_t frames) { return double(frames) / 60.0988139; }

std::string lb_clock(uint32_t frames) {
    const uint64_t s = uint64_t(lb_seconds(frames));
    char buf[24];
    std::snprintf(buf, sizeof buf, "%02u:%02u:%02u", unsigned(std::min<uint64_t>(s / 3600, 99)), unsigned(s / 60 % 60), unsigned(s % 60));
    return buf;
}

std::string lb_clock_cs(uint32_t frames) {
    const uint64_t cs = uint64_t(lb_seconds(frames) * 100.0);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%u:%02u:%02u.%02u", unsigned(cs / 360000), unsigned(cs / 6000 % 60), unsigned(cs / 100 % 60), unsigned(cs % 100));
    return buf;
}

std::string lb_score_digits(uint32_t score) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%06u0", unsigned(std::min<uint32_t>(score / 10, 999999)));
    return buf;
}

std::string lb_date(int64_t when) {
    if (when <= 0) return "";
    const time_t t = static_cast<time_t>(when);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm_buf);
    return buf;
}

uint32_t lb_score_from_digits(const uint8_t digits[6]) {
    uint32_t v = 0;
    for (int i = 5; i >= 0; i--) v = v * 10 + std::min<uint8_t>(digits[i], 9);
    return v * 10;
}
