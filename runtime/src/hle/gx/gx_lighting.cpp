// gx_lighting.cpp - Lighting and Channel Control
#include "gx_internal.h"
#include "runtime_log.h"

#include <cstdio>

namespace {

// GXLightObj is an opaque 64-byte guest object. The Revolution SDK leaves
// three words at the front unused, then stores the exact 13 XF light words
// loaded by GXLoadLightObjImm. Aurora's host-side GXLightObj implementation is
// compact (it starts at color), so a guest pointer must never be passed to the
// host implementation directly.
constexpr uint32_t kLightObjSize = 0x40;
constexpr uint32_t kColorOffset = 0x0c;
constexpr uint32_t kAttnAOffset = 0x10;
constexpr uint32_t kAttnKOffset = 0x1c;
constexpr uint32_t kPositionOffset = 0x28;
constexpr uint32_t kDirectionOffset = 0x34;

bool ValidateGuestLightObj(uint32_t addr, const char* operation) {
    if (addr != 0 && Memory::Contains(addr, kLightObjSize)) {
        return true;
    }
    RT_LOGF(RT_TAG_GX, "%s: invalid GXLightObj @0x%08X\n", operation, addr);
    std::fflush(stderr);
    return false;
}

float ReadGuestLightFloat(uint32_t addr, uint32_t offset) {
    return Memory::ReadFloat32(addr + offset);
}

void InitializeHostLightFromGuest(GXLightObj& host, uint32_t guestAddr) {
    GXInitLightColor(&host, DecodeGxColor(Memory::Read32(guestAddr + kColorOffset)));
    GXInitLightAttn(
        &host,
        ReadGuestLightFloat(guestAddr, kAttnAOffset + 0),
        ReadGuestLightFloat(guestAddr, kAttnAOffset + 4),
        ReadGuestLightFloat(guestAddr, kAttnAOffset + 8),
        ReadGuestLightFloat(guestAddr, kAttnKOffset + 0),
        ReadGuestLightFloat(guestAddr, kAttnKOffset + 4),
        ReadGuestLightFloat(guestAddr, kAttnKOffset + 8));
    GXInitLightPos(
        &host,
        ReadGuestLightFloat(guestAddr, kPositionOffset + 0),
        ReadGuestLightFloat(guestAddr, kPositionOffset + 4),
        ReadGuestLightFloat(guestAddr, kPositionOffset + 8));

    // GXInitLightDir negates its arguments before storing them. The guest
    // object already contains those stored XF direction components.
    GXInitLightDir(
        &host,
        -ReadGuestLightFloat(guestAddr, kDirectionOffset + 0),
        -ReadGuestLightFloat(guestAddr, kDirectionOffset + 4),
        -ReadGuestLightFloat(guestAddr, kDirectionOffset + 8));
}

} // namespace

// ============================================================================
// Light Object Load
// ============================================================================

extern "C" void GX__LoadLightObjImm_80170320(uint32_t la, uint32_t lid) {
    if (!ValidateGuestLightObj(la, "GXLoadLightObjImm")) return;
    GXLightObj host{};
    InitializeHostLightFromGuest(host, la);
    GXLoadLightObjImm(&host, static_cast<GXLightID>(lid));
}
PPC_NATIVE_OVERRIDE_VOID(80382BE4, GX__LoadLightObjImm_80170320, (uint32_t la, uint32_t lid), (la, lid));

// ============================================================================
// Channel Control
// ============================================================================

extern "C" void GX__SetChanAmbColor_8017039c(uint32_t c, uint32_t cp) {
    EnsureAuroraFrameActive();
    GXColor color = DecodeGxColor(Memory::Read32(cp));
    GXSetChanAmbColor((GXChannelID)c, color);
}
#if 0
PPC_NATIVE_OVERRIDE_VOID(801c6900, GX__SetChanAmbColor_8017039c, (uint32_t c, uint32_t cp), (c, cp));
#endif

extern "C" void GX__SetChanMatColor_80170474(uint32_t c, uint32_t cp) {
    EnsureAuroraFrameActive();
    GXColor color = DecodeGxColor(Memory::Read32(cp));
    GXSetChanMatColor((GXChannelID)c, color);
}
#if 0
PPC_NATIVE_OVERRIDE_VOID(801C69E0, GX__SetChanMatColor_80170474, (uint32_t c, uint32_t cp), (c, cp));
#endif

// Basara's SDK GXSetChanAmbColor (80382C60) / GXSetChanMatColor (80382D38) only merge the color
// into the __GXData shadow (+0xA8 amb, +0xB0 mat; COLORn = RGB, ALPHAn = alpha, COLORnAn = all)
// and set a dirty bit. The flush (__GXSetDirtyState) runs inside GXBegin / GXCallDisplayList,
// which are HLE, so the colors never reached aurora and lit meshes (characters) came out black.
// Mirror the shadow merge and push the merged word.
static void SetChanColorBasara(uint32_t chan, uint32_t cp, uint32_t shadowOffset, bool ambient) {
    static constexpr uint32_t kMask[6] = {0xFFFFFF00u, 0xFFFFFF00u, 0x000000FFu, 0x000000FFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    if (chan > 5 || cp == 0) return;
    const uint32_t gxData = Memory::Read32(kGXDataPtrAddr);
    if (gxData == 0) return;
    const uint32_t index = chan & 1u;
    const uint32_t addr = gxData + shadowOffset + index * 4u;
    const uint32_t merged = (Memory::Read32(addr) & ~kMask[chan]) | (Memory::Read32(cp) & kMask[chan]);
    Memory::Write32(addr, merged);
    EnsureAuroraFrameActive();
    const GXChannelID id = index ? GX_COLOR1A1 : GX_COLOR0A0;
    if (ambient) {
        GXSetChanAmbColor(id, DecodeGxColor(merged));
    } else {
        GXSetChanMatColor(id, DecodeGxColor(merged));
    }
}
extern "C" void GX__SetChanAmbColor_Basara(uint32_t c, uint32_t cp) { SetChanColorBasara(c, cp, 0xA8u, true); }
PPC_NATIVE_OVERRIDE_VOID(80382C60, GX__SetChanAmbColor_Basara, (uint32_t c, uint32_t cp), (c, cp));
extern "C" void GX__SetChanMatColor_Basara(uint32_t c, uint32_t cp) { SetChanColorBasara(c, cp, 0xB0u, false); }
PPC_NATIVE_OVERRIDE_VOID(80382D38, GX__SetChanMatColor_Basara, (uint32_t c, uint32_t cp), (c, cp));

extern "C" void GX__SetNumChans_8017054c(uint32_t n) { GXSetNumChans((u8)n); }
PPC_NATIVE_OVERRIDE_VOID(80382e10, GX__SetNumChans_8017054c, (uint32_t n), (n));

extern "C" void GX__SetChanCtrl_80170570(uint32_t ch, uint32_t en, uint32_t as, uint32_t ms, uint32_t lm, uint32_t df, uint32_t af) {
    GXSetChanCtrl((GXChannelID)ch, en!=0, (GXColorSrc)as, (GXColorSrc)ms, lm, (GXDiffuseFn)df, (GXAttnFn)af);
}
PPC_NATIVE_OVERRIDE_VOID(80382E34, GX__SetChanCtrl_80170570, (uint32_t ch, uint32_t en, uint32_t as, uint32_t ms, uint32_t lm, uint32_t df, uint32_t af), (ch, en, as, ms, lm, df, af));
