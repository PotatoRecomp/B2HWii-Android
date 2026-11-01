#pragma once

#include <cstdint>

struct CpuContext;

// Cheats for Sengoku Basara 2 Heroes (RBSJ08), first written by a friend's Linux port of this tree.
// Set in Config.toml [cheats] at start and from the Android launcher through JNI (SetFromLauncher);
// never applied in a LAN session, where the players' games must stay identical. The setters may be
// called from any thread.
namespace Cheats {

// The money cap the game clamps to when it adds a battle's earnings (func_80280B74).
constexpr uint32_t kMaxMoney = 9'999'999;

enum MoneyMode : int32_t { kMoneyOff = 0, kMoneyInfinite = 1, kMoneyOnce = 2 };

// False in a LAN session: a cheat there would make this player's game diverge.
bool Available();

// money: kMoney*; amount: the money Infinite holds and Once sets (clamped to kMaxMoney); onceSeq: a Once
// request is applied at the next frame when it differs from the last one seen.
void SetFromLauncher(int32_t money, uint32_t amount, uint32_t onceSeq, bool infiniteHp, bool infiniteBasara);

// Keeps the enabled cheats applied. Call once a frame on the guest thread.
void Apply();

} // namespace Cheats

// Argument hooks (abi_bridge.h ApplyRuntimeCallOptions).
void BasaraCheats_BeforeShopRefresh(CpuContext* ctx);
void BasaraCheats_BeforeHpSub(CpuContext* ctx);
void BasaraCheats_BeforeDamage(CpuContext* ctx);
void BasaraCheats_BeforeGaugeCalc(CpuContext* ctx);
