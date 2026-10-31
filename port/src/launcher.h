// Monster Party native port: the settings window shown before the game starts (Dear ImGui).
#pragma once
#include <string>
#include "settings.h"

// Shows the launcher (SDL video must already be initialised) and edits s in place.
// proto_ok: the Japanese prototype's ROM was found and checks out (else proto_why says why not),
// which enables the "play the prototype" checkbox.
// Returns true for START, false if the player quit. The window is closed before returning.
bool run_launcher(Settings& s, bool proto_ok, const std::string& proto_why);
