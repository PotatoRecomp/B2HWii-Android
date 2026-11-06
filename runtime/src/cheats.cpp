#include "cheats.h"

#include "memory.h"
#include "netplay_session.h"
#include "ppc_runtime.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Cheats {
namespace {

// The save/progress record at 0x80558F18 (0x8051DC10 + 0x40000 - 0x4CF8, getter 0x80155C24); its
// money is the u32 at +0x18. The shop draws it once and again only after a purchase.
constexpr uint32_t kMoneyAddr = 0x80558F30u;

// Read on the guest thread, set from Config.toml at start and from the launcher (JNI, UI thread).
std::atomic<int32_t> g_money{RuntimeConfigFile::CheatMoneyMode()};
std::atomic<uint32_t> g_amount{std::min(RuntimeConfigFile::CheatMoneyAmount(), kMaxMoney)};
// The last Once request seen; one at start is already old (it was applied in an earlier run).
std::atomic<uint32_t> g_onceSeq{RuntimeConfigFile::CheatMoneyOnceSeq()};
std::atomic<bool> g_oncePending{false};
std::atomic<bool> g_infiniteHp{RuntimeConfigFile::CheatInfiniteHp()};
std::atomic<bool> g_infiniteBasara{RuntimeConfigFile::CheatInfiniteBasara()};

// A playable character's class is cPl or one of its cPl00..cPlNN subclasses; the same characters as
// enemy officers are cEPlNN. The object's vtable starts with its RTTI, whose first word points at
// the class name.
bool IsPlayerCharacter(uint32_t obj) {
    uint32_t vtable = 0, rtti = 0, name = 0, prefix = 0;
    return Memory::TryRead32(obj, vtable) && Memory::TryRead32(vtable, rtti) &&
           Memory::TryRead32(rtti, name) && Memory::TryRead32(name, prefix) &&
           (prefix & 0xFFFFFF00u) == 0x63506C00u;  // "cPl"
}

} // namespace

bool Available() {
    return !NetplaySession::Active();
}

bool InfiniteHp() { return g_infiniteHp.load(std::memory_order_relaxed) && Available(); }
bool InfiniteBasara() { return g_infiniteBasara.load(std::memory_order_relaxed) && Available(); }
bool InfiniteMoney() { return g_money.load(std::memory_order_relaxed) == kMoneyInfinite && Available(); }

namespace {
float ClampDistance(float distance) {
    return distance >= 1.0f && distance <= 2.0f ? distance : 1.0f;
}
std::atomic<float> g_cameraDistance{[] {
    const char* env = std::getenv("BASARA_CAMERA_DISTANCE");
    return ClampDistance(env != nullptr ? std::strtof(env, nullptr) : RuntimeConfigFile::CheatCameraDistance());
}()};
} // namespace

float CameraDistance() { return Available() ? g_cameraDistance.load(std::memory_order_relaxed) : 1.0f; }
void SetCameraDistance(float distance) { g_cameraDistance = ClampDistance(distance); }

void SetFromLauncher(int32_t money, uint32_t amount, uint32_t onceSeq, bool infiniteHp, bool infiniteBasara) {
    g_money = std::clamp<int32_t>(money, kMoneyOff, kMoneyOnce);
    g_amount = std::min(amount, kMaxMoney);
    g_infiniteHp = infiniteHp;
    g_infiniteBasara = infiniteBasara;
    if (g_onceSeq.exchange(onceSeq) != onceSeq && money == kMoneyOnce) {
        g_oncePending = true;
    }
}

void Apply() {
    if (!Available()) {
        return;
    }
    if (InfiniteMoney()) {
        // Logged each time the game had changed it (boot, loading a save, spending), the first few times.
        static int s_logged = 0;
        uint32_t before = 0;
        Memory::TryRead32(kMoneyAddr, before);
        const uint32_t amount = g_amount.load(std::memory_order_relaxed);
        if (Memory::TryWrite32(kMoneyAddr, amount) && before != amount && s_logged < 8) {
            ++s_logged;
            RT_LOGF(RT_TAG_OS, "cheats: infinite money holds %u (was %u)\n", amount, before);
        }
    } else if (g_oncePending.exchange(false)) {
        const uint32_t amount = g_amount.load(std::memory_order_relaxed);
        if (Memory::TryWrite32(kMoneyAddr, amount)) {
            RT_LOGF(RT_TAG_OS, "cheats: money set to %u once\n", amount);
        }
    }
}

} // namespace Cheats

// cPl::hpSub (0x80247BE0) takes r4 off the player's HP at +0x500 and clamps it at 0; with Infinite HP
// it is asked to take nothing. (Layout as PS2's cPl, 8 bytes later: max HP +0x56C, state +0x4ED.)
namespace {
// The first time a cheat acts, for the log (tester reports).
void LogFirst(std::atomic<bool>& done, const char* what, uint32_t value) {
    if (!done.exchange(true)) {
        RT_LOGF(RT_TAG_OS, "cheats: %s (%u)\n", what, value);
    }
}
std::atomic<bool> g_loggedHp{false}, g_loggedBasara{false};
} // namespace

void BasaraCheats_BeforeHpSub(CpuContext* ctx) {
    if (Cheats::InfiniteHp()) {
        if (ctx->gpr[4] != 0) LogFirst(g_loggedHp, "infinite HP blocked damage", ctx->gpr[4]);
        ctx->gpr[4] = 0;
    }
}

// Damage (0x8012C850, virtual slot 0x70) takes r4 off the HP at +0x500 of the object in r3, and
// marks it dead below 0. With Infinite HP a player character is asked to take nothing.
void BasaraCheats_BeforeDamage(CpuContext* ctx) {
    if (Cheats::InfiniteHp() && Cheats::IsPlayerCharacter(ctx->gpr[3])) {
        if (ctx->gpr[4] != 0) LogFirst(g_loggedHp, "infinite HP blocked damage", ctx->gpr[4]);
        ctx->gpr[4] = 0;
    }
}

// cPl::plGaugeCalc (0x8022D4D8) runs once a frame per character. The gauge is +0x98C of max +0x994
// and its stock +0x99C of max +0x998, as cPl::spAdd (0x80247824) saturates them; while a Basara
// attack runs (+0x7AC bit 0x100) the calc drains the gauge, so it is left alone to let the attack end.
void BasaraCheats_BeforeGaugeCalc(CpuContext* ctx) {
    const uint32_t pl = ctx->gpr[3];
    uint32_t flags = 0, gaugeMax = 0, stockMax = 0;
    if (Cheats::InfiniteBasara() && Cheats::IsPlayerCharacter(pl) &&
        Memory::TryRead32(pl + 0x7AC, flags) && !(flags & 0x100) &&
        Memory::TryRead32(pl + 0x994, gaugeMax) && Memory::TryRead32(pl + 0x998, stockMax)) {
        LogFirst(g_loggedBasara, "infinite Basara fills the gauge", gaugeMax);
        Memory::TryWrite32(pl + 0x98C, gaugeMax);
        Memory::TryWrite32(pl + 0x99C, stockMax);
    }
}

// The shop's purchase stores money - price and then refreshes the shop; Infinite money puts the money
// back first, so the shop never shows less. Only the call in the purchase returns to 0x80048BD0.
void BasaraCheats_BeforeShopRefresh(CpuContext* ctx) {
    if (ctx->lr == 0x80048BD0u && Cheats::InfiniteMoney()) {
        Memory::TryWrite32(Cheats::kMoneyAddr, Cheats::g_amount.load(std::memory_order_relaxed));
    }
}

// Camera distance. The camera (heap object, pointer at SDA 806626A0; view matrix +0xA7C, eye +0xAEC,
// target +0xACC, up +0xBF0) builds its view through 802BF3AC, which negates the up vector into its
// 0x20-byte frame (+0x08..+0x13) and calls C_MTXLookAt(m r3, eye r4, up r5, target r6). The eye moved
// away from the target, along the line between them, is written into the unused rest of that frame
// (+0x14..+0x1F) and passed instead.
namespace {
float ReadF32(uint32_t addr, bool& ok) {
    uint32_t bits = 0;
    ok = ok && Memory::TryRead32(addr, bits);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void WriteF32(uint32_t addr, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    Memory::TryWrite32(addr, bits);
}
} // namespace

// The farther eye also stands in the camera's own eye (+0xAEC) for the rest of the frame, so whatever
// culls or picks a level of detail from it sees what the view shows; the game's eye goes back before
// the next camera update (8004FE9C), which builds on it. One slot per camera (two in split screen).
namespace {
struct SavedEye {
    uint32_t cam = 0;
    uint32_t bits[3] = {};
};
SavedEye g_savedEyes[2];
} // namespace

void BasaraCheats_BeforeCameraUpdate(CpuContext* ctx) {
    for (SavedEye& saved : g_savedEyes) {
        if (saved.cam != 0 && saved.cam == ctx->gpr[3]) {
            for (int i = 0; i < 3; ++i) {
                Memory::TryWrite32(saved.cam + 0xAECu + 4u * i, saved.bits[i]);
            }
            saved.cam = 0;
        }
    }
}

void BasaraCheats_BeforeLookAt(CpuContext* ctx) {
    const uint32_t sp = ctx->gpr[1];
    const uint32_t eye = ctx->gpr[4];
    const uint32_t target = ctx->gpr[6];
    uint32_t caller = 0;
    if (ctx->gpr[5] != sp + 8u || !Memory::TryRead32(sp + 0x24u, caller)) {
        return;  // not the wrapper's frame
    }
    const float k = Cheats::CameraDistance();
    if (k == 1.0f) {
        return;
    }
    // Only the battle view: the main camera's final LookAt (8004FE9C, return 8004FF1C) in battle mode
    // (+0xC3C 0) with no scripted event (+0x24 0). The stage intro runs in mode 5 (about 570 frames on
    // a Windows test) and cutscenes through the event cameras, which keep the game's framing.
    {
        const uint32_t cam = ctx->gpr[3] - 0xA7Cu;
        uint32_t mode = ~0u, event = ~0u;
        if (caller != 0x8004FF1Cu || !Memory::TryRead32(cam + 0xC3Cu, mode) ||
            !Memory::TryRead32(cam + 0x24u, event) || mode != 0u || event != 0u) {
            return;
        }
    }
    bool ok = true;
    float e[3], t[3];
    for (int i = 0; i < 3; ++i) {
        e[i] = ReadF32(eye + 4u * i, ok);
        t[i] = ReadF32(target + 4u * i, ok);
    }
    if (!ok) {
        return;
    }
    float moved[3];
    for (int i = 0; i < 3; ++i) {
        moved[i] = t[i] + (e[i] - t[i]) * k;
    }
    // Never lower than the game's eye: a camera that looks up at its target would back down into the
    // ground. The up vector is the one handed to C_MTXLookAt (the wrapper's frame, +0x08).
    float up[3];
    for (int i = 0; i < 3; ++i) {
        up[i] = ReadF32(sp + 8u + 4u * i, ok);
    }
    const float upLen = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (ok && upLen > 1e-6f) {
        float drop = 0.0f;
        for (int i = 0; i < 3; ++i) {
            drop += (moved[i] - e[i]) * up[i] / upLen;
        }
        if (drop < 0.0f) {
            for (int i = 0; i < 3; ++i) {
                moved[i] -= drop * up[i] / upLen;
            }
        }
    }
    const uint32_t out = sp + 0x14u;
    for (int i = 0; i < 3; ++i) {
        WriteF32(out + 4u * i, moved[i]);
    }
    ctx->gpr[4] = out;
    const uint32_t cam = ctx->gpr[3] - 0xA7Cu;
    if (eye == cam + 0xAECu) {
        SavedEye* slot = nullptr;
        for (SavedEye& saved : g_savedEyes) {
            if (saved.cam == cam || (slot == nullptr && saved.cam == 0)) slot = &saved;
        }
        if (slot != nullptr && slot->cam == 0) {
            slot->cam = cam;
            for (int i = 0; i < 3; ++i) {
                Memory::TryRead32(eye + 4u * i, slot->bits[i]);
            }
            for (int i = 0; i < 3; ++i) {
                WriteF32(eye + 4u * i, moved[i]);
            }
        }
    }
}
