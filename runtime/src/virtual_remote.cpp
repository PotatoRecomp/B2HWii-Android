#include "virtual_remote.h"

#include "input_bindings.h"
#include "keybinds.h"
#include "memory.h"
#include "runtime_log.h"

#include <dolphin/pad.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace VirtualRemote {
namespace {

// WPAD_BUTTON_* bits as the game reads them from KPADStatus.hold.
constexpr uint32_t kWpadLeft = 0x0001, kWpadRight = 0x0002, kWpadDown = 0x0004, kWpadUp = 0x0008,
                   kWpadPlus = 0x0010, kWpadTwo = 0x0100, kWpadOne = 0x0200, kWpadA = 0x0800,
                   kWpadMinus = 0x1000, kWpadHome = 0x8000;

// The remote's D-pad by screen direction. Held sideways with the D-pad under the left thumb, the
// pad's "up" arrow points left, "right" points up, "down" points right and "left" points down.
constexpr uint32_t kScreenLeft = kWpadUp, kScreenRight = kWpadDown, kScreenUp = kWpadRight,
                   kScreenDown = kWpadLeft;

// WPAD_CL_BUTTON_* bits (KPADStatus.ex_status.cl.hold), as in wii_remote_input.cpp.
constexpr uint32_t kClUp = 0x0001, kClLeft = 0x0002, kClZR = 0x0004, kClX = 0x0008, kClA = 0x0010, kClY = 0x0020,
                   kClB = 0x0040, kClZL = 0x0080, kClR = 0x0200, kClPlus = 0x0400, kClHome = 0x0800,
                   kClMinus = 0x1000, kClL = 0x2000, kClDown = 0x4000, kClRight = 0x8000;

// The game's own button word, built from the Classic buttons each frame (BASARA_LOG_CLASSIC).
constexpr uint32_t kGameButtonHold = 0x80564770;

// What the game's motion code looks for (dGameKeyCore_c::setShakeY and the tilt code after
// setConfigKey): a shake is acc.y changing by at least 0.28 g between consecutive frames on four
// frames (gaps of up to two allowed), then a five-frame cool-down; the tilt angle is -acc.z, through
// KPAD's acc_vertical, at 16384 angle units per g. A shake request swings y by +-1.2 g around rest
// for at least kShakeFrames reads, enough for one detection, and holding it keeps shaking.
constexpr float kShakeSwingG = 1.2f;
constexpr int kShakeFrames = 10;
constexpr float kMaxTiltRadians = 0.7853982f;  // 45 degrees
constexpr float kTiltFollow = 0.3f;            // share of the way to the requested tilt per frame
constexpr int kTriggerThreshold = 128;         // of 255
// aurora's PADRead polls SDL; the game probes and reads all four channels each frame.
constexpr uint64_t kPadRefreshNs = 4'000'000;

struct Intent {
    uint32_t cl = 0;                  // WPAD_CL_BUTTON_* bits
    float lStick[2] = {};             // -1..1, +y up
    float rStick[2] = {};
    uint8_t triggerL = 0;             // 0..255
    uint8_t triggerR = 0;
    bool shake = false;               // the phone being shaken: accelerometer only
    float tilt = 0.0f;                // -1 counter-clockwise (left) .. 1 clockwise (right)
};

struct Motion {
    int shakeFramesLeft = 0;
    uint32_t shakePhase = 0;
    float tilt = 0.0f;
};

// Written by the Android UI thread (SetTouchState), read on the guest thread. The *SinceRead
// latches keep a press shorter than one 60 Hz input read (a quick tap) visible for one read.
std::atomic<uint32_t> g_touchButtons{0};
std::atomic<uint32_t> g_touchButtonsSinceRead{0};
std::atomic<bool> g_touchShake{false};
std::atomic<bool> g_touchShakeSinceRead{false};
std::atomic<float> g_touchTilt{0.0f};
std::array<std::atomic<float>, 4> g_touchSticks{};  // left x, y, right x, y

std::mutex g_mutex;
std::array<PADStatus, PAD_MAX_CONTROLLERS> g_pads{};
uint64_t g_padsReadNs = 0;
bool g_keyboardOnAPort = false;
std::array<Motion, PAD_MAX_CONTROLLERS> g_motion{};
std::array<bool, PAD_MAX_CONTROLLERS> g_announced{};

// Test hook for checking the gamepad mapping without hardware: BASARA_VIRTUAL_GAMEPAD=1 attaches an
// SDL virtual gamepad driven by generated/b/vgamepad.txt ("B=hex [A=lx,ly,rx,ry,lt,rt]", SDL's button
// order and axes from -1 to 1).
void PumpVirtualGamepad() {
    static const bool enabled = std::getenv("BASARA_VIRTUAL_GAMEPAD") != nullptr;
    if (!enabled) {
        return;
    }
    static SDL_Joystick* joystick = nullptr;
    static bool attached = false;
    if (!attached) {
        attached = true;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Virtual Test Gamepad";
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        joystick = id != 0 ? SDL_OpenJoystick(id) : nullptr;
        RT_LOGF(RT_TAG_RUNTIME, "virtual gamepad: id=%u opened=%d %s\n", static_cast<unsigned>(id),
                joystick != nullptr ? 1 : 0, joystick != nullptr ? "" : SDL_GetError());
    }
    static uint64_t lastCheck = 0;
    const uint64_t now = SDL_GetTicksNS();
    if (joystick == nullptr || now - lastCheck < 15000000) {
        return;
    }
    lastCheck = now;
    FILE* fp = fopen("generated/b/vgamepad.txt", "r");
    if (fp == nullptr) {
        return;
    }
    char buf[96]{};
    const bool read = fgets(buf, sizeof(buf), fp) != nullptr;
    fclose(fp);
    uint32_t buttons = 0;
    float axes[6] = {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f};
    if (read) {
        sscanf(buf, "B=%x", &buttons);
        if (const char* a = strstr(buf, "A=")) {
            sscanf(a + 2, "%f,%f,%f,%f,%f,%f", &axes[0], &axes[1], &axes[2], &axes[3], &axes[4], &axes[5]);
        }
    }
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
        SDL_SetJoystickVirtualButton(joystick, i, (buttons >> i) & 1u);
    }
    for (int i = 0; i < 6; ++i) {
        SDL_SetJoystickVirtualAxis(joystick, i, static_cast<Sint16>(axes[i] * 32767.0f));
    }
}

void RefreshPadsLocked() {
    const uint64_t now = SDL_GetTicksNS();
    if (g_padsReadNs != 0 && now - g_padsReadNs < kPadRefreshNs) {
        return;
    }
    g_padsReadNs = now;
    PumpVirtualGamepad();
    PADRead(g_pads.data());
    InputBindings::Apply(g_pads.data());
    g_keyboardOnAPort = false;
    for (uint32_t port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        uint32_t count = 0;
        if (PADGetKeyButtonBindings(port, &count) != nullptr) {
            g_keyboardOnAPort = true;
        }
    }
}

// One stick from four keys, keeping a diagonal on the unit circle.
void KeyStick(float* stick, bool up, bool down, bool left, bool right) {
    const float x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    const float y = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
    const float scale = x != 0.0f && y != 0.0f ? 0.7071068f : 1.0f;
    stick[0] = x * scale;
    stick[1] = y * scale;
}

// The built-in layout for port 1, used while no port has aurora's keyboard switched on.
Intent KeyboardIntent() {
    Intent intent;
    if (SDL_GetKeyboardFocus() == nullptr) {
        return intent;
    }
    // The keys come from the F10 bar (Config.toml [keyboard]); none count while one is being
    // picked by a capture, or until the key picked is let go.
    if (Keybinds::Suppressed()) {
        return intent;
    }
    using namespace Keybinds;
    const auto bit = [&intent](int action, uint32_t cl) {
        if (Held(action)) intent.cl |= cl;
    };
    bit(kUp, kClUp);
    bit(kDown, kClDown);
    bit(kLeft, kClLeft);
    bit(kRight, kClRight);
    bit(Keybinds::kA, kClA);
    bit(Keybinds::kB, kClB);
    bit(Keybinds::kX, kClX);
    bit(Keybinds::kY, kClY);
    bit(Keybinds::kZL, kClZL);
    bit(Keybinds::kZR, kClZR);
    bit(kPlus, kClPlus);
    bit(kMinus, kClMinus);
    bit(kHome, kClHome);
    if (Held(Keybinds::kL)) {
        intent.cl |= kClL;
        intent.triggerL = 255;
    }
    if (Held(Keybinds::kR)) {
        intent.cl |= kClR;
        intent.triggerR = 255;
    }
    KeyStick(intent.lStick, Held(kLStickUp), Held(kLStickDown), Held(kLStickLeft), Held(kLStickRight));
    KeyStick(intent.rStick, Held(kRStickUp), Held(kRStickDown), Held(kRStickLeft), Held(kRStickRight));
    return intent;
}

// The on-screen Classic Controller and phone motion set through SetTouchState and SetTouchSticks.
Intent TouchIntent() {
    Intent intent;
    const uint32_t touch = g_touchButtons.load(std::memory_order_relaxed) |
                           g_touchButtonsSinceRead.exchange(0, std::memory_order_relaxed);
    const auto bit = [&](uint32_t button, uint32_t cl) {
        if (touch & button) intent.cl |= cl;
    };
    bit(kTouchLeft, kClLeft);
    bit(kTouchRight, kClRight);
    bit(kTouchUp, kClUp);
    bit(kTouchDown, kClDown);
    bit(kTouchA, kClA);
    bit(kTouchB, kClB);
    bit(kTouchX, kClX);
    bit(kTouchY, kClY);
    bit(kTouchZL, kClZL);
    bit(kTouchZR, kClZR);
    bit(kTouchPlus, kClPlus);
    bit(kTouchMinus, kClMinus);
    bit(kTouchHome, kClHome);
    if (touch & kTouchL) {
        intent.cl |= kClL;
        intent.triggerL = 255;
    }
    if (touch & kTouchR) {
        intent.cl |= kClR;
        intent.triggerR = 255;
    }
    intent.lStick[0] = g_touchSticks[0].load(std::memory_order_relaxed);
    intent.lStick[1] = g_touchSticks[1].load(std::memory_order_relaxed);
    intent.rStick[0] = g_touchSticks[2].load(std::memory_order_relaxed);
    intent.rStick[1] = g_touchSticks[3].load(std::memory_order_relaxed);
    intent.shake = g_touchShake.load(std::memory_order_relaxed) ||
                   g_touchShakeSinceRead.exchange(false, std::memory_order_relaxed);
    intent.tilt = std::clamp(g_touchTilt.load(std::memory_order_relaxed), -1.0f, 1.0f);
    return intent;
}

// File-based virtual pad automation (works without window focus). A line of words, or
// "CL=hex [LS=x,y]" for raw Classic bits and a left stick.
Intent AutomationIntent() {
    static uint64_t lastCheck = 0;
    static Intent file;
    const uint64_t now = SDL_GetTicksNS();
    if (now - lastCheck > 15000000) {
        lastCheck = now;
        file = {};
        FILE* fp = fopen("generated/b/vpad.txt", "r");
        if (fp != nullptr) {
            char buf[64]{};
            if (fgets(buf, sizeof(buf), fp) != nullptr) {
                for (char* p = buf; *p; ++p) *p = static_cast<char>(toupper(*p));
                if (strncmp(buf, "CL=", 3) == 0) {
                    file.cl = static_cast<uint32_t>(std::strtoul(buf + 3, nullptr, 16));
                    if (const char* ls = strstr(buf, "LS=")) {
                        sscanf(ls + 3, "%f,%f", &file.lStick[0], &file.lStick[1]);
                    }
                    if (const char* rs = strstr(buf, "RS=")) {
                        sscanf(rs + 3, "%f,%f", &file.rStick[0], &file.rStick[1]);
                    }
                } else {
                    if (strstr(buf, "X") || strstr(buf, "2") || strstr(buf, "A")) file.cl |= kClA;
                    if (strstr(buf, "START") || strstr(buf, "PLUS") || strstr(buf, "ENTER")) file.cl |= kClPlus;
                    if (strstr(buf, "UP")) file.cl |= kClUp;
                    if (strstr(buf, "DOWN")) file.cl |= kClDown;
                    if (strstr(buf, "LEFT")) file.cl |= kClLeft;
                    if (strstr(buf, "RIGHT")) file.cl |= kClRight;
                    if (strstr(buf, "1") || strstr(buf, "B") || strstr(buf, "RUN")) file.cl |= kClB;
                }
            }
            fclose(fp);
        }
    }
    return file;
}

// A gamepad (after the user's remapping) as the Classic Controller, by position as in Dolphin's
// Classic layout: A east, B south, X north, Y west, the triggers as L and R, the shoulders as ZL
// and ZR. PADStatus has no left shoulder or guide button, so those two come from SDL directly.
Intent PadIntent(uint32_t chan, const PADStatus& pad) {
    Intent intent;
    const uint32_t b = pad.button;
    const auto bit = [&](uint32_t button, uint32_t cl) {
        if (b & button) intent.cl |= cl;
    };
    bit(PAD_BUTTON_LEFT, kClLeft);
    bit(PAD_BUTTON_RIGHT, kClRight);
    bit(PAD_BUTTON_UP, kClUp);
    bit(PAD_BUTTON_DOWN, kClDown);
    bit(PAD_BUTTON_B, kClA);
    bit(PAD_BUTTON_A, kClB);
    bit(PAD_BUTTON_Y, kClX);
    bit(PAD_BUTTON_X, kClY);
    bit(PAD_BUTTON_START, kClPlus);
    bit(PAD_TRIGGER_Z, kClZR);
    if (pad.extButton & PAD_BUTTON_BACK) intent.cl |= kClMinus;
    const s32 index = PADGetIndexForPort(chan);
    if (SDL_Gamepad* gamepad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr) {
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) intent.cl |= kClZL;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE)) intent.cl |= kClHome;
    }
    intent.triggerL = (b & PAD_TRIGGER_L) != 0 ? 255 : pad.triggerLeft;
    intent.triggerR = (b & PAD_TRIGGER_R) != 0 ? 255 : pad.triggerRight;
    if (intent.triggerL >= kTriggerThreshold) intent.cl |= kClL;
    if (intent.triggerR >= kTriggerThreshold) intent.cl |= kClR;
    intent.lStick[0] = std::clamp(pad.stickX / 127.0f, -1.0f, 1.0f);
    intent.lStick[1] = std::clamp(pad.stickY / 127.0f, -1.0f, 1.0f);
    intent.rStick[0] = std::clamp(pad.substickX / 127.0f, -1.0f, 1.0f);
    intent.rStick[1] = std::clamp(pad.substickY / 127.0f, -1.0f, 1.0f);
    return intent;
}

// Accelerometer for the requested motion, in KPAD's frame (rest: y = -1). Tilting clockwise lowers
// the right end and raises the left one, where the remote's pointer end is, so gravity shows up on
// the remote's long axis (KPAD z) as +sin(angle).
void ApplyMotion(Motion& motion, const Intent& intent, WiiRemoteInput::KpadSample& sample) {
    if (intent.shake) {
        motion.shakeFramesLeft = std::max(motion.shakeFramesLeft, kShakeFrames);
    }
    motion.tilt += (intent.tilt - motion.tilt) * kTiltFollow;
    if (std::fabs(motion.tilt) < 0.001f) {
        motion.tilt = 0.0f;
    }
    const float angle = motion.tilt * kMaxTiltRadians;
    sample.acc[0] = 0.0f;
    sample.acc[1] = -std::cos(angle);
    sample.acc[2] = std::sin(angle);
    if (motion.shakeFramesLeft > 0) {
        --motion.shakeFramesLeft;
        sample.acc[1] += (motion.shakePhase++ & 1u) != 0 ? kShakeSwingG : -kShakeSwingG;
    }
}

void AnnounceLocked(uint32_t chan, bool pad) {
    if (g_announced[chan]) {
        return;
    }
    g_announced[chan] = true;
    const char* source = "keyboard";
    if (pad) {
        const s32 index = PADGetIndexForPort(chan);
        SDL_Gamepad* gamepad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
        const char* name = gamepad != nullptr ? SDL_GetGamepadName(gamepad) : nullptr;
        source = name != nullptr ? name : g_keyboardOnAPort ? "keyboard (settings bindings)" : "controller";
    }
    RT_LOGF(RT_TAG_RUNTIME, "player %u: %s as a sideways Wii Remote\n", chan + 1, source);
}

} // namespace

void SetTouchState(uint32_t touchButtons, bool shake, float tilt) {
    g_touchButtons.store(touchButtons, std::memory_order_relaxed);
    g_touchButtonsSinceRead.fetch_or(touchButtons, std::memory_order_relaxed);
    g_touchShake.store(shake, std::memory_order_relaxed);
    if (shake) {
        g_touchShakeSinceRead.store(true, std::memory_order_relaxed);
    }
    g_touchTilt.store(std::isfinite(tilt) ? tilt : 0.0f, std::memory_order_relaxed);
}

void SetTouchSticks(float leftX, float leftY, float rightX, float rightY) {
    g_touchSticks[0].store(leftX, std::memory_order_relaxed);
    g_touchSticks[1].store(leftY, std::memory_order_relaxed);
    g_touchSticks[2].store(rightX, std::memory_order_relaxed);
    g_touchSticks[3].store(rightY, std::memory_order_relaxed);
}

bool Present(uint32_t chan) {
    if (chan >= PAD_MAX_CONTROLLERS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    RefreshPadsLocked();
    return chan == 0 || g_pads[chan].err == PAD_ERR_NONE;
}

bool Sample(uint32_t chan, WiiRemoteInput::KpadSample& sample) {
    if (chan >= PAD_MAX_CONTROLLERS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    RefreshPadsLocked();
    const bool pad = g_pads[chan].err == PAD_ERR_NONE;
    if (chan != 0 && !pad) {
        return false;
    }
    AnnounceLocked(chan, pad);
    Intent intent;
    if (pad) {
        intent = PadIntent(chan, g_pads[chan]);
    }
    const auto merge = [&intent](const Intent& other) {
        intent.cl |= other.cl;
        intent.shake = intent.shake || other.shake;
        if (other.tilt != 0.0f) {
            intent.tilt = other.tilt;
        }
        for (int axis = 0; axis < 2; ++axis) {
            if (other.lStick[axis] != 0.0f) intent.lStick[axis] = other.lStick[axis];
            if (other.rStick[axis] != 0.0f) intent.rStick[axis] = other.rStick[axis];
        }
        intent.triggerL = std::max(intent.triggerL, other.triggerL);
        intent.triggerR = std::max(intent.triggerR, other.triggerR);
    };
    if (chan == 0 && !g_keyboardOnAPort) {
        merge(KeyboardIntent());
    }
    if (chan == 0) {
        merge(TouchIntent());
        merge(AutomationIntent());
    }
    if (InputBindings::InputBlocked()) {
        intent = {};  // the settings overlay owns input; the remote stays connected at rest
    }
    sample = {};
    ApplyMotion(g_motion[chan], intent, sample);
    // Basara only goes past its controller screen with an extension on the remote (WPADProbe type
    // 1 or 2), so the virtual remote always carries a Classic Controller. The remote's own buttons
    // follow it, for the code that still reads those.
    sample.hasClassic = true;
    sample.clHold = intent.cl;
    const auto wpad = [&](uint32_t cl, uint32_t button) {
        if (intent.cl & cl) sample.hold |= button;
    };
    wpad(kClUp, kScreenUp);
    wpad(kClDown, kScreenDown);
    wpad(kClLeft, kScreenLeft);
    wpad(kClRight, kScreenRight);
    wpad(kClA, kWpadTwo);
    wpad(kClB, kWpadOne);
    wpad(kClX, kWpadA);
    wpad(kClPlus, kWpadPlus);
    wpad(kClMinus, kWpadMinus);
    wpad(kClHome, kWpadHome);
    for (int axis = 0; axis < 2; ++axis) {
        sample.clLStick[axis] = intent.lStick[axis];
        sample.clRStick[axis] = intent.rStick[axis];
        sample.clLStickRaw[axis] = static_cast<int16_t>(intent.lStick[axis] * 511.0f);
        sample.clRStickRaw[axis] = static_cast<int16_t>(intent.rStick[axis] * 511.0f);
    }
    sample.clTriggerL = intent.triggerL;
    sample.clTriggerR = intent.triggerR;
    static const bool logClassic = std::getenv("BASARA_LOG_CLASSIC") != nullptr;
    if (logClassic && chan == 0) {
        // The flash manager's busy pointer, message code, state and answer tell the save flow apart.
        static uint32_t last[7] = {};
        const uint32_t analog = static_cast<uint32_t>(static_cast<int32_t>(intent.lStick[0] * 7) + 8) |
                                static_cast<uint32_t>(static_cast<int32_t>(intent.lStick[1] * 7) + 8) << 4 |
                                static_cast<uint32_t>(static_cast<int32_t>(intent.rStick[0] * 7) + 8) << 8 |
                                static_cast<uint32_t>(static_cast<int32_t>(intent.rStick[1] * 7) + 8) << 12 |
                                static_cast<uint32_t>(intent.triggerL) << 16 | static_cast<uint32_t>(intent.triggerR) << 24;
        const uint32_t now[7] = {intent.cl, Memory::Read32(kGameButtonHold), Memory::Read32(0x80580E00),
                                 Memory::Read32(0x80580F3C), Memory::Read32(0x80580F40), Memory::Read32(0x80580F70), analog};
        if (std::memcmp(now, last, sizeof(now)) != 0) {
            std::memcpy(last, now, sizeof(now));
            RT_LOGF(RT_TAG_RUNTIME, "classic: cl=%04X ls=%.2f,%.2f rs=%.2f,%.2f lt=%d rt=%d game=%08X mgr=%08X code=%08X state=%X answer=%d\n",
                    now[0], intent.lStick[0], intent.lStick[1], intent.rStick[0], intent.rStick[1],
                    intent.triggerL, intent.triggerR, now[1], now[2], now[3], now[4],
                    static_cast<int32_t>(now[5]));
        }
    }
    return true;
}

} // namespace VirtualRemote
