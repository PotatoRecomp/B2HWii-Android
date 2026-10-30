#include "keybinds.h"

#include "runtime_config.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace Keybinds {
namespace {

struct Default {
    const char* key;   // Config.toml [keyboard] key
    const char* name;  // shown in the Keybinds tab
    std::vector<SDL_Scancode> codes;
};

const std::array<Default, kActionCount>& Defaults() {
    static const std::array<Default, kActionCount> defaults = {{
        {"up", "Up", {SDL_SCANCODE_UP}},
        {"down", "Down", {SDL_SCANCODE_DOWN}},
        {"left", "Left", {SDL_SCANCODE_LEFT}},
        {"right", "Right", {SDL_SCANCODE_RIGHT}},
        {"classic_a", "A", {SDL_SCANCODE_X, SDL_SCANCODE_SPACE}},
        {"classic_b", "B", {SDL_SCANCODE_Z, SDL_SCANCODE_LSHIFT}},
        {"classic_x", "X", {SDL_SCANCODE_V}},
        {"classic_y", "Y", {SDL_SCANCODE_C, SDL_SCANCODE_LCTRL}},
        {"classic_l", "L", {SDL_SCANCODE_Q}},
        {"classic_r", "R", {SDL_SCANCODE_E}},
        {"classic_zl", "ZL", {SDL_SCANCODE_1}},
        {"classic_zr", "ZR", {SDL_SCANCODE_3}},
        {"plus", "+", {SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE}},
        {"minus", "-", {SDL_SCANCODE_MINUS, SDL_SCANCODE_TAB}},
        {"classic_home", "Home", {SDL_SCANCODE_H}},
        {"lstick_up", "L Stick Up", {SDL_SCANCODE_W}},
        {"lstick_down", "L Stick Down", {SDL_SCANCODE_S}},
        {"lstick_left", "L Stick Left", {SDL_SCANCODE_A}},
        {"lstick_right", "L Stick Right", {SDL_SCANCODE_D}},
        {"rstick_up", "R Stick Up", {SDL_SCANCODE_I}},
        {"rstick_down", "R Stick Down", {SDL_SCANCODE_K}},
        {"rstick_left", "R Stick Left", {SDL_SCANCODE_J}},
        {"rstick_right", "R Stick Right", {SDL_SCANCODE_L}},
    }};
    return defaults;
}

std::mutex g_mutex;
bool g_loaded = false;
std::array<std::vector<SDL_Scancode>, kActionCount> g_codes;
int g_capturing = -1;
std::vector<bool> g_heldAtCapture;  // keys already down when the capture began
bool g_suppress = false;            // a capture just ended: ignore the keyboard until all keys are up

std::vector<SDL_Scancode> Parse(const std::string& text) {
    std::vector<SDL_Scancode> codes;
    const auto lookup = [&text](size_t start, size_t length) {
        const std::string name = RuntimeConfigFile::Trim(std::string_view(text).substr(start, length));
        return name.empty() ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(name.c_str());
    };
    size_t start = 0;
    while (start < text.size()) {
        const size_t comma = text.find(',', start);
        size_t next = comma == std::string::npos ? text.size() : comma + 1;
        SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
        if (comma != std::string::npos) {
            // A key's own name can end in the separator ("," or "Keypad ,").
            code = lookup(start, comma + 1 - start);
            if (code != SDL_SCANCODE_UNKNOWN && next < text.size() && text[next] == ',') {
                ++next;
            }
        }
        if (code == SDL_SCANCODE_UNKNOWN) {
            code = lookup(start, comma == std::string::npos ? std::string::npos : comma - start);
        }
        if (code != SDL_SCANCODE_UNKNOWN) {
            codes.push_back(code);
        }
        start = next;
    }
    return codes;
}

std::string Format(const std::vector<SDL_Scancode>& codes, const char* separator) {
    std::string text;
    for (const SDL_Scancode code : codes) {
        if (!text.empty()) {
            text += separator;
        }
        text += SDL_GetScancodeName(code);
    }
    return text;
}

void LoadLocked() {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    for (int i = 0; i < kActionCount; ++i) {
        const std::string saved = RuntimeConfigFile::KeyboardBinding(Defaults()[i].key);
        g_codes[i] = saved.empty() ? Defaults()[i].codes : Parse(saved);
        if (g_codes[i].size() > kKeysPerAction) {
            g_codes[i].resize(kKeysPerAction);
        }
    }
}

void SaveLocked(int action) {
    // An empty value means "use the default" when loading, so an unbound action is written out.
    const std::string text = Format(g_codes[action], ",");
    RuntimeConfigFile::SetKeyboardBinding(Defaults()[action].key, text.empty() ? "None" : text);
}

bool AnyKeyDown() {
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int i = 0; keys != nullptr && i < count; ++i) {
        if (keys[i]) {
            return true;
        }
    }
    return false;
}

} // namespace

const char* ActionName(int action) {
    return action >= 0 && action < kActionCount ? Defaults()[action].name : "";
}

std::string KeyLabel(int action) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (action < 0 || action >= kActionCount) {
        return {};
    }
    const std::string text = Format(g_codes[action], " / ");
    return text.empty() ? "None" : text;
}

bool Held(int action) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (g_capturing >= 0 || g_suppress || action < 0 || action >= kActionCount) {
        return false;
    }
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (const SDL_Scancode code : g_codes[action]) {
        if (keys != nullptr && code < count && keys[code]) {
            return true;
        }
    }
    return false;
}

std::string KeyName(int action, int slot) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (action < 0 || action >= kActionCount || slot < 0 || slot >= static_cast<int>(g_codes[action].size())) {
        return "None";
    }
    return SDL_GetScancodeName(g_codes[action][slot]);
}

int Bind(int action, int slot, int scancode) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (action < 0 || action >= kActionCount || slot < 0 || slot >= kKeysPerAction) {
        return -1;
    }
    std::vector<SDL_Scancode>& codes = g_codes[action];
    if (scancode <= 0) {
        if (slot < static_cast<int>(codes.size())) {
            codes.erase(codes.begin() + slot);
        }
        SaveLocked(action);
        return -1;
    }
    const auto code = static_cast<SDL_Scancode>(scancode);
    int moved = -1;
    for (int other = 0; other < kActionCount; ++other) {
        std::vector<SDL_Scancode>& held = g_codes[other];
        const auto it = std::find(held.begin(), held.end(), code);
        if (other != action && it != held.end()) {
            held.erase(it);
            SaveLocked(other);
            moved = other;
        }
    }
    if (std::find(codes.begin(), codes.end(), code) == codes.end()) {
        if (slot < static_cast<int>(codes.size())) {
            codes[slot] = code;
        } else {
            codes.push_back(code);
        }
    }
    SaveLocked(action);
    return moved;
}

void BeginCapture(int action) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    g_heldAtCapture.assign(static_cast<size_t>(count), false);
    for (int i = 0; keys != nullptr && i < count; ++i) {
        g_heldAtCapture[i] = keys[i];
    }
    g_capturing = action;
}

int Capturing() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_capturing;
}

bool PollCapture() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_capturing < 0) {
        return true;
    }
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int i = 0; keys != nullptr && i < count; ++i) {
        const bool before = i < static_cast<int>(g_heldAtCapture.size()) && g_heldAtCapture[i];
        if (!keys[i]) {
            if (before) {
                g_heldAtCapture[i] = false;  // released: a later press of it counts
            }
            continue;
        }
        if (before) {
            continue;
        }
        const auto code = static_cast<SDL_Scancode>(i);
        if (code != SDL_SCANCODE_ESCAPE) {
            g_codes[g_capturing] = {code};
            SaveLocked(g_capturing);
        }
        g_capturing = -1;
        g_suppress = true;
        return true;
    }
    return false;
}

bool Suppressed() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_suppress && !AnyKeyDown()) {
        g_suppress = false;
    }
    return g_capturing >= 0 || g_suppress;
}

void SuppressUntilReleased() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_suppress = true;
}

void ResetDefaults() {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    for (int i = 0; i < kActionCount; ++i) {
        g_codes[i] = Defaults()[i].codes;
        SaveLocked(i);
    }
}

} // namespace Keybinds
