#include "platform.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>

static std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

FILE* mp_fopen(const std::string& path, const char* mode) {
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
}

bool mp_rename(const std::string& from, const std::string& to) {
    return MoveFileExW(wide(from).c_str(), wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool mp_remove(const std::string& path) { return _wremove(wide(path).c_str()) == 0; }

bool mp_is_absolute(const std::string& p) {
    if (p.size() >= 3 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) return true;
    return p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/');   // \\server\share
}

std::string mp_file_name(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

void mp_open_folder(const std::string& dir) { SDL_OpenURL(dir.c_str()); }   // ShellExecute opens folders in Explorer

// One standard stream: if it already goes somewhere (redirected to a file or pipe, or a console
// program), leave it; a GUI program started from a console gets that console; otherwise the log.
static void reopen_stream(DWORD which, FILE* stream, bool have_console, const std::wstring& log, bool& log_open) {
    HANDLE h = GetStdHandle(which);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) return;
    if (have_console) {
        if (_wfreopen(L"CONOUT$", L"w", stream)) return;
    }
    if (log.empty()) return;
    // the first stream truncates the log, the second appends to it
    if (_wfreopen(log.c_str(), log_open ? L"a" : L"w", stream)) {
        log_open = true;
        setvbuf(stream, nullptr, _IONBF, 0);   // unbuffered: the two streams share a file, and a crash keeps the log
    }
}

void mp_setup_console() {
    const bool out_set = [] { HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); return h != nullptr && h != INVALID_HANDLE_VALUE; }();
    const bool err_set = [] { HANDLE h = GetStdHandle(STD_ERROR_HANDLE); return h != nullptr && h != INVALID_HANDLE_VALUE; }();
    if (out_set && err_set) return;
    const bool have_console = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    std::wstring log;
    if (!have_console) {
        const std::string dir = mp_config_dir();
        if (!dir.empty()) {
            SDL_CreateDirectory(dir.c_str());
            log = wide(dir + "monsterparty.log");
        }
    }
    bool log_open = false;
    reopen_stream(STD_OUTPUT_HANDLE, stdout, have_console, log, log_open);
    reopen_stream(STD_ERROR_HANDLE, stderr, have_console, log, log_open);
}

#else  // ---------------------------------------------------------------- Linux and other POSIX systems

FILE* mp_fopen(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }
bool mp_rename(const std::string& from, const std::string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }
bool mp_remove(const std::string& path) { return std::remove(path.c_str()) == 0; }
bool mp_is_absolute(const std::string& p) { return !p.empty() && p[0] == '/'; }

std::string mp_file_name(const std::string& p) {
    const size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

void mp_open_folder(const std::string& dir) { SDL_OpenURL(("file://" + dir).c_str()); }
void mp_setup_console() {}

#endif

bool mp_read_file(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = mp_fopen(path, "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

std::string mp_config_dir() {
    std::string dir;
    if (const char* m = std::getenv("MP_CONFIG_HOME"); m && *m) {
        dir = m;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir + "monsterparty/";
    }
#ifndef _WIN32
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/monsterparty/";
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.config/monsterparty/";
#endif
    // Windows: %APPDATA%\NESdsm\monsterparty\ (made if it isn't there); elsewhere a last resort
    if (char* p = SDL_GetPrefPath("NESdsm", "monsterparty")) { dir = p; SDL_free(p); }
    return dir;
}
