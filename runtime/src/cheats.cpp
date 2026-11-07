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
std::atomic<bool> g_unlockAll{RuntimeConfigFile::CheatUnlockAll()};

// Unlock all. The save lives at 0x8051DC10; its accessors are at 0x80155BE0..0x80156630.
// - Record (getter 0x80155C24): stages 0..48 open = bits of the u64 at +0x00 (+0x08 = shown as new),
//   stages 49..78 open = bits of the u32 at +0x148 (+0x14C = new); 0x80140D04 reads them, 0x802848F8
//   opens one. Items: one byte each at +0x38, 0xB7 of them (setter 0x80156338).
// - Characters (getter 0x80155BE0): id 0..31 -> slot through the table at 0x80497E00 (-1 = none), a
//   0x1588-byte record per slot at 0x80530928. +0x3C and +0x3D: the character is open (0x80154148;
//   0x8009FF64 and 0x800A00FC open them); +0x20[8] skill levels 0..3, each character's skills listed
//   at 0x80497E80 (10 bytes per id, 0xFF = none; 0x80155CD8, the new-game setup 0x8002F33C gives the
//   first two level 1); +0x28[8] weapons and +0x30[1..3] (setters 0x80156368, 0x801563DC; 0x800A0330
//   and 0x800A01E8 give them at level 1).
// Each such byte keeps a value in its low 7 bits and a "new" mark in bit 7. Unlock all only fills the
// empty ones with 1, so nothing the player has is changed and no "new" marks appear.
constexpr uint32_t kRecordAddr = 0x80558F18u;
constexpr uint32_t kCharRecords = 0x80530928u;
constexpr uint32_t kCharStride = 0x1588u;
constexpr uint32_t kCharSlots = 0x80497E00u;
constexpr uint32_t kCharSkills = 0x80497E80u;
constexpr uint32_t kCharIds = 32u;
constexpr uint32_t kItems = 0xB7u;
constexpr uint32_t kHighStageBits = (1u << (0x4Fu - 49u)) - 1u;  // stages 49..78

// Sets the byte at addr to 1 when its value (low 7 bits) is 0. True when it changed.
bool FillEmpty(uint32_t addr) {
    const uint32_t word = addr & ~3u;
    const uint32_t shift = (3u - (addr & 3u)) * 8u;
    uint32_t value = 0;
    if (!Memory::TryRead32(word, value) || ((value >> shift) & 0x7Fu) != 0) {
        return false;
    }
    return Memory::TryWrite32(word, value | (1u << shift));
}

bool OrWord(uint32_t addr, uint32_t bits) {
    uint32_t value = 0;
    if (!Memory::TryRead32(addr, value) || (value & bits) == bits) {
        return false;
    }
    return Memory::TryWrite32(addr, value | bits);
}

uint8_t ReadByte(uint32_t addr) {
    uint32_t value = 0;
    Memory::TryRead32(addr & ~3u, value);
    return static_cast<uint8_t>(value >> ((3u - (addr & 3u)) * 8u));
}

// Returns how many entries it opened.
uint32_t UnlockAllNow() {
    uint32_t opened = 0;
    opened += OrWord(kRecordAddr + 0x00u, 0xFFFFFFFFu);
    opened += OrWord(kRecordAddr + 0x04u, 0xFFFFFFFFu);
    opened += OrWord(kRecordAddr + 0x148u, kHighStageBits);
    for (uint32_t item = 1; item < kItems; ++item) {
        opened += FillEmpty(kRecordAddr + 0x38u + item);
    }
    for (uint32_t id = 0; id < kCharIds; ++id) {
        uint32_t slot = 0;
        if (!Memory::TryRead32(kCharSlots + id * 4u, slot) || static_cast<int32_t>(slot) < 0) {
            continue;
        }
        const uint32_t chr = kCharRecords + slot * kCharStride;
        opened += FillEmpty(chr + 0x3Cu);
        opened += FillEmpty(chr + 0x3Du);
        for (uint32_t i = 0; i < 10; ++i) {
            const uint8_t skill = ReadByte(kCharSkills + id * 10u + i);
            if (skill < 8) {
                opened += FillEmpty(chr + 0x20u + skill);
            }
        }
        for (uint32_t i = 0; i < 8; ++i) {
            opened += FillEmpty(chr + 0x28u + i);
        }
        for (uint32_t i = 1; i < 4; ++i) {
            opened += FillEmpty(chr + 0x30u + i);
        }
    }
    return opened;
}

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

void SetFromLauncher(int32_t money, uint32_t amount, uint32_t onceSeq, bool infiniteHp, bool infiniteBasara,
                     bool unlockAll) {
    g_money = std::clamp<int32_t>(money, kMoneyOff, kMoneyOnce);
    g_amount = std::min(amount, kMaxMoney);
    g_infiniteHp = infiniteHp;
    g_infiniteBasara = infiniteBasara;
    g_unlockAll = unlockAll;
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
    // Once a second: a save loaded later replaces what was opened before it.
    static uint32_t s_frame = 0;
    if (g_unlockAll.load(std::memory_order_relaxed) && ++s_frame % 60u == 1u) {
        static int s_logged = 0;
        const uint32_t opened = UnlockAllNow();
        if (opened != 0 && s_logged < 8) {
            ++s_logged;
            RT_LOGF(RT_TAG_OS, "cheats: unlock all opened %u entries\n", opened);
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

// Camera distance. The main camera's update (8004FE9C; camera on the heap, pointer at SDA 806626A0)
// builds its projection through the MTXPerspective wrapper 802BF3F4 (f1 = fovY in degrees from
// +0xC30, r3 = cam + 0xA2C, r31 = cam). The field of view is widened as if the eye stood k times
// farther back: tan(fov'/2) = k * tan(fov/2). The eye itself stays where the game's camera collision
// put it: moving it back (1.1) went into rising ground or walls, and the field vanished (Mali-G57 tester).
// Culling follows the game's projection matrix, so the wider view culls correctly.
void BasaraCheats_BeforePerspective(CpuContext* ctx) {
    const float k = Cheats::CameraDistance();
    if (k == 1.0f) {
        return;
    }
    const uint32_t cam = ctx->gpr[31];
    if (ctx->gpr[3] != cam + 0xA2Cu) {
        return;  // not the main camera update's call (8004FF48)
    }
    // Only the battle view: battle mode (+0xC3C 0) with no scripted event (+0x24 0). The stage intro
    // runs in mode 5 and cutscenes through the event cameras, which keep the game's framing.
    uint32_t mode = ~0u, event = ~0u, fovBits = 0;
    if (!Memory::TryRead32(cam + 0xC3Cu, mode) || !Memory::TryRead32(cam + 0x24u, event) ||
        !Memory::TryRead32(cam + 0xC30u, fovBits) || mode != 0u || event != 0u) {
        return;
    }
    float fov;
    std::memcpy(&fov, &fovBits, sizeof(fov));
    if (!(fov > 1.0f && fov < 170.0f) || static_cast<float>(ctx->fpr[1].d) != fov) {
        return;
    }
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float wide = 2.0f * std::atan(std::tan(fov * 0.5f * kDegToRad) * k) / kDegToRad;
    ctx->fpr[1].d = static_cast<double>(std::min(wide, 120.0f));
    static std::atomic<bool> s_logged{false};
    if (!s_logged.exchange(true)) {
        RT_LOGF(RT_TAG_OS, "cheats: camera distance %.2fx widens the battle view %.1f -> %.1f degrees\n", k, fov,
                std::min(wide, 120.0f));
    }
}
