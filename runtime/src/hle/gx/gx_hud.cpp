// Basara 2D layer (HUD, markers over characters, menus): which game function draws what, and where.
//
// BASARA_LOG_2D=1: every immediate-mode draw under the 2D orthographic view is attributed to the
// function that called GXBegin (and its caller); the horizontal extent of its positions is
// gathered per call site and printed every 5 seconds. Used to sort call sites into HUD at the
// left / right edge, markers that follow a character, and full-screen pictures for widescreen.
#include "gx_internal.h"

#include "guest_interrupt_context.h"
#include "memory.h"
#include "runtime_log.h"

#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace {

struct SiteStats {
    uint64_t draws = 0;
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
};

bool g_ortho2D = false;
uint32_t g_site = 0;
uint32_t g_parent = 0;
std::map<std::pair<uint32_t, uint32_t>, SiteStats> g_sites;
std::chrono::steady_clock::time_point g_lastDump;

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BASARA_LOG_2D");
        return v && *v == '1';
    }();
    return on;
}

void Dump() {
    std::fprintf(stderr, "[2d] ---- call sites (caller parent: draws, x range, y range in 448x392 units)\n");
    for (const auto& [key, s] : g_sites) {
        std::fprintf(stderr, "[2d] %08X %08X: %8llu  x %6.1f..%6.1f  y %6.1f..%6.1f\n", key.first, key.second,
                     static_cast<unsigned long long>(s.draws), s.minX, s.maxX, s.minY, s.maxY);
    }
    g_sites.clear();
}

} // namespace

void BasaraHud_SetProjection(bool ortho, float xScale) {
    g_ortho2D = ortho && xScale != 0.0f && 2.0f / std::abs(xScale) > 150.0f;
}

void BasaraHud_Begin() {
    if (!Enabled()) {
        return;
    }
    g_site = 0;
    g_parent = 0;
    if (!g_ortho2D) {
        return;
    }
    if (const CpuContext* cpu = TryGetCpuContext()) {
        g_site = cpu->lr;
        uint32_t back = 0;
        if (Memory::TryRead32(cpu->gpr[1], back) && back != 0) {
            Memory::TryRead32(back + 4, g_parent);
        }
    }
    g_sites[{g_site, g_parent}].draws++;
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastDump > std::chrono::seconds(5)) {
        g_lastDump = now;
        Dump();
    }
}

void BasaraHud_Position(float x, float y) {
    if (g_site == 0) {
        return;
    }
    SiteStats& s = g_sites[{g_site, g_parent}];
    s.minX = std::min(s.minX, x);
    s.maxX = std::max(s.maxX, x);
    s.minY = std::min(s.minY, y);
    s.maxY = std::max(s.maxY, y);
}
