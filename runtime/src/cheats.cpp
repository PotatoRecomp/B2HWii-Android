#include "cheats.h"

#include "memory.h"
#include "netplay_session.h"
#include "ppc_runtime.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <algorithm>
#include <atomic>

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
