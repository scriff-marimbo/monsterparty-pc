// What differs between Linux and Windows: opening files by UTF-8 path, the per-user config folder,
// what counts as an absolute path, and showing a folder in the file manager.
//
// Every path in the port is a UTF-8 std::string (SDL's paths are UTF-8 too). On Linux that's what
// fopen takes. On Windows fopen reads paths in the ANSI code page, so a path with characters
// outside it (a user folder named "José", or in Japanese) can't be opened: these helpers convert
// to UTF-16 and use the wide-character calls instead. Use them for every file the game reads or
// writes, never std::fopen / std::ifstream directly.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

FILE* mp_fopen(const std::string& path, const char* mode);
bool mp_read_file(const std::string& path, std::vector<unsigned char>& out);   // whole file; false if it can't be read
// replaces `to` if it exists (std::rename doesn't on Windows)
bool mp_rename(const std::string& from, const std::string& to);
bool mp_remove(const std::string& path);

// The folder for settings.ini, leaderboards.ini, palettes/ and soundfonts/, ending in a separator:
//   Linux:   $XDG_CONFIG_HOME/monsterparty/, else ~/.config/monsterparty/
//   Windows: %APPDATA%\NESdsm\monsterparty\  (HOME and XDG_CONFIG_HOME are ignored: Git Bash and
//            MSYS set HOME, which would hide the settings somewhere unexpected)
// $MP_CONFIG_HOME overrides both (like XDG_CONFIG_HOME: the folder is $MP_CONFIG_HOME/monsterparty/);
// the unit tests use it for a throwaway folder.
std::string mp_config_dir();

// "/x" on Linux; also "C:\x", "C:/x" and "\\server\share" on Windows
bool mp_is_absolute(const std::string& path);
// the part after the last '/' (or '\' on Windows)
std::string mp_file_name(const std::string& path);
// open a folder in the system's file manager (Files, Explorer)
void mp_open_folder(const std::string& dir);

// Windows only: with a GUI-subsystem executable there's no console, so printf output goes nowhere.
// Started from a console (cmd, PowerShell) the output goes there; otherwise stdout and stderr are
// written to monsterparty.log in the config folder. Does nothing on Linux.
void mp_setup_console();
