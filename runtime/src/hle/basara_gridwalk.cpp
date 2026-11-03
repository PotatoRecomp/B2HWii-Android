// Basara's collision grid walk (80029844): appends to a u16 list (count, then cell indices) every
// grid cell the segment start(r5) -> end(r6) crosses, stepping one cell at a time until it reaches
// the end cell. The guest has no bound, and both callers (8002BC48, 8002C06C) keep the list on their
// own stack with room for 39 cells. A tester's story stage on horseback ended in a walk that never met
// its end cell; it ran on through the saved registers of 8002BC48 and its callers (u16 cells 22
// apart, the grid width) until 8021E39C made a virtual call through a smashed r31 and crashed.
// Same steps here, stopping at the list's room; the first cut-off walks are logged.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kCellSizeAddr = 0x80663100u;  // f31
constexpr uint32_t kScaleAddr = 0x80663118u;     // f0 / f31 = cells per unit
constexpr uint32_t kZeroAddr = 0x80663114u;      // f30, compared with the segment's dx / dz
constexpr uint32_t kEdgeTest = 0x8002953Cu;      // segment against one cell edge
constexpr uint32_t kMaxCells = 39;                // callers' stack lists: 0x50 bytes after the count
std::atomic<uint32_t> g_logged{0};

int32_t Fctiwz(float value) {
    if (std::isnan(value)) return INT32_MIN;
    if (value >= 2147483647.0f) return INT32_MAX;
    if (value <= -2147483648.0f) return INT32_MIN;
    return static_cast<int32_t>(value);
}

bool EdgeHit(CpuContext* ctx, uint32_t owner, uint32_t start, uint32_t end, float f1, float f2,
             float f3, float f4) {
    ctx->gpr[3] = owner;
    ctx->gpr[4] = start;
    ctx->gpr[5] = end;
    ctx->fpr[1].d = f1;
    ctx->fpr[2].d = f2;
    ctx->fpr[3].d = f3;
    ctx->fpr[4].d = f4;
    InvokeIndirectCpu(kEdgeTest, ctx);
    return ctx->gpr[3] != 0;
}

void LogCut(CpuContext* ctx, uint32_t start, uint32_t end, uint32_t from, uint32_t to) {
    if (g_logged.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    std::cerr << "[basara] collision grid walk cut at " << kMaxCells << " cells: start ("
              << Memory::ReadFloat32(start) << "," << Memory::ReadFloat32(start + 4u) << ","
              << Memory::ReadFloat32(start + 8u) << ") end (" << Memory::ReadFloat32(end) << ","
              << Memory::ReadFloat32(end + 4u) << "," << Memory::ReadFloat32(end + 8u) << ") cells "
              << from << " -> " << to << std::hex << " lr=0x" << ctx->lr << std::dec << std::endl;
}

} // namespace

extern "C" void BasaraGridWalk_HLE_80029844(CpuContext* ctx) {
    const uint32_t owner = ctx->gpr[3];
    const uint32_t list = ctx->gpr[4];
    const uint32_t start = ctx->gpr[5];
    const uint32_t end = ctx->gpr[6];

    const float cell = Memory::ReadFloat32(kCellSizeAddr);
    const float scale = Memory::ReadFloat32(kScaleAddr) / cell;
    const float zero = Memory::ReadFloat32(kZeroAddr);
    const uint32_t width = Memory::Read16(Memory::Read32(owner + 0x534u) + 4u);

    const int32_t endZ = Fctiwz(Memory::ReadFloat32(end + 8u) * scale);
    const int32_t startZ = Fctiwz(Memory::ReadFloat32(start + 8u) * scale);
    const int32_t endX = Fctiwz(Memory::ReadFloat32(end) * scale);
    const int32_t startX = Fctiwz(Memory::ReadFloat32(start) * scale);

    const uint32_t target = static_cast<uint32_t>(endX + endZ * static_cast<int32_t>(width)) & 0xFFFFu;
    uint32_t current = static_cast<uint32_t>(startX + startZ * static_cast<int32_t>(width)) & 0xFFFFu;

    uint32_t count = Memory::Read16(list);
    auto append = [&](uint32_t value) {
        Memory::Write16(list + 2u + count * 2u, static_cast<uint16_t>(value));
        ++count;
        Memory::Write16(list, static_cast<uint16_t>(count));
    };
    ctx->gpr[3] = 0;
    if (count >= kMaxCells) {
        LogCut(ctx, start, end, current, target);
        return;
    }
    append(current);
    if (current == target) {
        ctx->gpr[3] = 0;
        return;
    }

    float x = static_cast<float>(static_cast<uint16_t>(startX)) * cell;
    float z = static_cast<float>(static_cast<uint16_t>(startZ)) * cell;
    const float dx = Memory::ReadFloat32(end) - Memory::ReadFloat32(start);
    const float dz = Memory::ReadFloat32(end + 8u) - Memory::ReadFloat32(start + 8u);

    while (current != target) {
        if (count >= kMaxCells) {
            LogCut(ctx, start, end, current, target);
            break;
        }
        uint32_t edges = 0;
        if (dx < zero) {
            if (EdgeHit(ctx, owner, start, end, x, z, x, z + cell)) edges |= 1;
        } else {
            const float x1 = x + cell;
            if (EdgeHit(ctx, owner, start, end, x1, z + cell, x1, z)) edges |= 4;
        }
        if (dz < zero) {
            if (EdgeHit(ctx, owner, start, end, x + cell, z, x, z)) edges |= 8;
        } else {
            const float z1 = z + cell;
            if (EdgeHit(ctx, owner, start, end, x, z1, x + cell, z1)) edges |= 2;
        }

        if (edges & 1) {
            x -= cell;
            current = (current - 1u) & 0xFFFFu;
        } else if (edges & 4) {
            x += cell;
            current = (current + 1u) & 0xFFFFu;
        }
        if (edges & 2) {
            z += cell;
            current = (current + width) & 0xFFFFu;
        } else if (edges & 8) {
            z -= cell;
            current = (current - width) & 0xFFFFu;
        }

        if (edges == 0) {
            append(target);
            break;
        }
        append(current);
    }
    ctx->gpr[3] = 0;
}
PPC_NATIVE_OVERRIDE_VOID(80029844, BasaraGridWalk_HLE_80029844, (CpuContext* ctx), (ctx));
