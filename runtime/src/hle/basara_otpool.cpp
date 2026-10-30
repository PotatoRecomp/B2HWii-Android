// Basara's render-bucket pool (80352F04): obj->count(+4)+1 entries of 16 bytes are taken from the
// pool at *0x80662898 (next free index *0x8066289C, capacity *0x806628A0), zeroed (words 0/4/8),
// chained through +8 and stored in obj->entries(+0). The guest never checks the capacity; in the
// SJ01 demo scene the count table at 0x8049C270 (.data) gets overwritten, a garbage count zeroed
// memory from MEM1 up to 0xCC000000 and crashed. Same steps here, but a count that does not fit
// the pool, or a table word that differs from the DOL, is logged and replaced (table word repaired).
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kPoolAddr = 0x80662898u;
constexpr uint32_t kIndexAddr = 0x8066289Cu;
constexpr uint32_t kCapacityAddr = 0x806628A0u;
constexpr uint32_t kEntrySize = 16;
constexpr uint32_t kCountTableAddr = 0x8049C270u;  // 9 slot counts, set up by 802BFF08
constexpr uint32_t kCountTable[9] = {1, 1, 1, 1, 0x400, 0x80, 8, 0xA, 0x40};
constexpr uint32_t kLogLimit = 16;
std::atomic<uint32_t> g_logged{0};

void LogBadCount(CpuContext* ctx, uint32_t obj, uint32_t count, uint32_t index, uint32_t capacity) {
    if (g_logged.fetch_add(1, std::memory_order_relaxed) >= kLogLimit) return;
    // Callers 802BFF60/802C0090: r27 = bucket set {+4/+8 obj lists, +0xC count table, +0x10 slots},
    // r28 = slot index.
    const uint32_t set = ctx->gpr[27];
    const uint32_t table = Memory::Read32(set + 0xCu);
    std::cerr << "[basara] bucket count bad: obj=0x" << std::hex << obj << " count=0x" << count
              << " index=0x" << index << " cap=0x" << capacity << " lr=0x" << ctx->lr << " r27=0x" << set
              << " r28=0x" << ctx->gpr[28] << " r29=0x" << ctx->gpr[29] << " set={0x"
              << Memory::Read32(set + 4u) << ",0x" << Memory::Read32(set + 8u) << ",0x" << table << ",0x"
              << Memory::Read32(set + 0x10u) << "} table=";
    for (uint32_t i = 0; i < 9; ++i) std::cerr << (i ? "," : "") << Memory::Read32(table + i * 4u);
    std::cerr << std::dec << std::endl;
}

} // namespace

extern "C" void BasaraBucketPool_HLE_80352f04(CpuContext* ctx) {
    const uint32_t obj = ctx->gpr[3];
    uint32_t count = Memory::Read32(obj + 4u);
    const uint32_t index = Memory::Read32(kIndexAddr);
    const uint32_t capacity = Memory::Read32(kCapacityAddr);
    // Callers 802BFF60/802C0090 copy table[r28] to obj+4 just before the call. The table is
    // constant game data, so a word that differs from the DOL value is foreign and is repaired.
    const uint32_t slot = ctx->gpr[28];
    if (Memory::Read32(ctx->gpr[27] + 0xCu) == kCountTableAddr && slot < 9u &&
        Memory::Read32(kCountTableAddr + slot * 4u) == count && count != kCountTable[slot]) {
        LogBadCount(ctx, obj, count, index, capacity);
        count = kCountTable[slot];
        Memory::Write32(kCountTableAddr + slot * 4u, count);
        Memory::Write32(obj + 4u, count);
    }
    if (index >= capacity || static_cast<uint64_t>(count) + 1u > capacity - index) {
        LogBadCount(ctx, obj, count, index, capacity);
        count = 0u;
        Memory::Write32(obj + 4u, count);
    }
    const uint32_t entries = count + 1u;
    const uint32_t base = Memory::Read32(kPoolAddr) + index * kEntrySize;
    for (uint32_t i = 0; i < entries; ++i) {
        const uint32_t entry = base + i * kEntrySize;
        Memory::Write32(entry, 0u);
        Memory::Write32(entry + 4u, 0u);
        Memory::Write32(entry + 8u, 0u);
    }
    Memory::Write32(kIndexAddr, index + entries);
    Memory::Write32(obj, base);
    for (uint32_t i = 0; i < count; ++i) {
        Memory::Write32(base + i * kEntrySize + 8u, base + (i + 1u) * kEntrySize);
    }
    ctx->gpr[0] = count;
    ctx->gpr[4] = 0;
    ctx->gpr[5] = entries;
    ctx->gpr[8] = base;
}
PPC_NATIVE_OVERRIDE_VOID(80352F04, BasaraBucketPool_HLE_80352f04, (CpuContext* ctx), (ctx));
