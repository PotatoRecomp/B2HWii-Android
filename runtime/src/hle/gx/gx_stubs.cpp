#include "gx_internal.h"
#include "runtime_log.h"

extern "C" void __GXSetSUTexRegs();

// ============================================================================
// FIFO Write Helpers
// ============================================================================

extern "C" void GX_HLE_FIFO_WriteFloat(float val) {
    u32 raw; std::memcpy(&raw, &val, 4);
    try { HleFifoWrite(raw, 4); } catch (...) { RT_LOGF(RT_TAG_GX, "FIFO write float failed\n"); }
}

extern "C" void GX_HLE_FIFO_Write32(uint32_t val) { HleFifoWrite(val, 4); }
extern "C" void GX_HLE_FIFO_Write16(uint16_t val) { HleFifoWrite(static_cast<u32>(val), 2); }
extern "C" void GX_HLE_FIFO_Write8(uint8_t val) { HleFifoWrite(static_cast<u32>(val), 1); }

extern "C" void GX__SetDrawSync_8016ed08(uint32_t token) {
    (void)token;
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) {
        if (Memory::Read32(gd + 0x5FCu)) GX__SetDirtyState_8016ee78();
        Memory::Write16(gd + 2, 0);
    } } catch (...) {}
}

extern "C" void GX__SetDrawSync_8016e9fc(uint32_t token) { GX__SetDrawSync_8016ed08(token); }
#if 0  // NSMBW: GXSetDrawSync is not linked into NSMBW (was MKW-only)
#if 0
PPC_NATIVE_OVERRIDE_VOID(8016e9fc, GX__SetDrawSync_8016e9fc, (uint32_t token), (token));
#endif
#endif

extern "C" void GX__FinishInterruptHandler_8016ed94() {
    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 0x0Au, static_cast<uint16_t>(Memory::Read16(gd + 0x0Au) | 0x0008u));
        Memory::Write8(kGxDrawDoneFlagAddr, 1);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80381ad0, GX__FinishInterruptHandler_8016ed94, (), ());

extern "C" void GX__DrawDone_8016eab0() {
    try { Memory::Write8(kGxDrawDoneFlagAddr, 0); } catch (...) {}
    GXDrawDone(); GX__FinishInterruptHandler_8016ed94();
}
PPC_NATIVE_OVERRIDE_VOID(803817ec, GX__DrawDone_8016eab0, (), ());

// Basara paces frames with the GX breakpoint interrupt (GXEnableBreakPt + GXSetBreakPtCallback) and
// the PE draw-sync token read by its post-retrace callback. HLE GP work is done as soon as the CPU
// writes it, so an armed breakpoint counts as reached and its callback runs on the next VI retrace
// (vi.cpp), and the token reads back as the last one set.
static constexpr uint32_t kGxBreakPtPtrAddr = 0x80662db8u;       // BreakPointPtr (Basara)
static constexpr uint32_t kGxBreakPtCallbackAddr = 0x80662dbcu;  // BreakPointCB (Basara)
// With the GX thread on, the GP lags the CPU: the breakpoint counts as reached, and the token as
// written, only once the GX thread has run everything submitted before them. Reporting them early
// lets the game reuse vertex/matrix/texture buffers the GX thread has yet to read (flicker).
extern "C" uint64_t AuroraGxThreadMark();
extern "C" bool AuroraGxThreadMarkReached(uint64_t ticket);
extern "C" void AuroraGxThreadPostCall(void (*fn)(uint32_t), uint32_t arg);
static std::atomic<bool> s_breakPtArmed{false};
static std::atomic<uint64_t> s_breakPtMark{0};
static std::atomic<uint32_t> s_drawSyncToken{0};

extern "C" void GX__EnableBreakPt_803804e8(uint32_t ptr) {
    try { Memory::Write32(kGxBreakPtPtrAddr, ptr); } catch (...) {}
    s_breakPtMark.store(AuroraGxThreadMark());
    s_breakPtArmed.store(true);
}
PPC_NATIVE_OVERRIDE_VOID(803804e8, GX__EnableBreakPt_803804e8, (uint32_t ptr), (ptr));

extern "C" void GX__DisableBreakPt_80380588() {
    s_breakPtArmed.store(false);
    try { Memory::Write32(kGxBreakPtPtrAddr, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80380588, GX__DisableBreakPt_80380588, (), ());

// Returns the breakpoint callback to run when a breakpoint is armed (and disarms it, as the CP
// interrupt handler does), else 0.
extern "C" uint32_t GX_HLE_TakeBreakPtCallback() {
    if (!s_breakPtArmed.load()) return 0;
    if (!AuroraGxThreadMarkReached(s_breakPtMark.load())) return 0;  // GP not there yet: a later retrace
    if (!s_breakPtArmed.exchange(false)) return 0;
    uint32_t cb = 0;
    Memory::TryRead32(kGxBreakPtCallbackAddr, cb);
    return cb;
}

extern "C" void GX__SetDrawSync_8038172c(uint32_t token) {
    AuroraGxThreadPostCall([](uint32_t t) { s_drawSyncToken.store(t); }, token & 0xFFFFu);
    GX__SetDrawSync_8016ed08(token);
}
PPC_NATIVE_OVERRIDE_VOID(8038172c, GX__SetDrawSync_8038172c, (uint32_t token), (token));

extern "C" uint32_t GX__ReadDrawSync_803817e0() { return s_drawSyncToken.load(); }
PPC_NATIVE_OVERRIDE(803817e0, GX__ReadDrawSync_803817e0, uint32_t, (), ());

extern "C" void GX__PixModeSync_8016eb70() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
    GXPixModeSync();
}
// Basara: address checked against the register fields this function writes.
PPC_NATIVE_OVERRIDE_VOID(803818ac, GX__PixModeSync_8016eb70, (), ());

// ============================================================================
// Hardware Revision / Thread Query - No-ops
// ============================================================================

extern "C" void __GX__InitRevisionBits_8016b720() {}
PPC_NATIVE_OVERRIDE_VOID(8037ec5c, __GX__InitRevisionBits_8016b720, (), ());

// ============================================================================
// Texture State Management - Aurora handles internally
// ============================================================================

extern "C" void __GX__SetSUTexRegs_801712f0() {
    __GXSetSUTexRegs();
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(803838dc, __GX__SetSUTexRegs_801712f0, (), ());

extern "C" void __GX__SetTmemConfig_80171458(uint32_t mode) {
    // TMEM layout configuration - Aurora manages internally
    (void)mode;
}
PPC_NATIVE_OVERRIDE_VOID(80383A44, __GX__SetTmemConfig_80171458, (uint32_t mode), (mode));

extern "C" void __GX__FlushTextureState_80171c28() {
    // BP texture state flush - Aurora handles via API
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80384214, __GX__FlushTextureState_80171c28, (), ());

// ============================================================================
// Copy Configuration - No-ops for features Aurora doesn't use
// ============================================================================

extern "C" void GX__SetDispCopyFrame2Field_8016f5f8(uint32_t f) {
    GXSetDispCopyFrame2Field(f);
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFCFFFu) | ((f & 3u) << 12));
            Memory::Write32(gd + 0x24Cu, Memory::Read32(gd + 0x24Cu) & 0xFFFFCFFFu);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80382310, GX__SetDispCopyFrame2Field_8016f5f8, (uint32_t f), (f));

extern "C" void GX__SetCopyClamp_8016f618(uint32_t c) {
    GXSetCopyClamp(static_cast<GXFBClamp>(c));
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            const uint32_t clamp = c & 3u;
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFFFFCu) | clamp);
            Memory::Write32(gd + 0x24Cu, (Memory::Read32(gd + 0x24Cu) & 0xFFFFFFFCu) | clamp);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80382330, GX__SetCopyClamp_8016f618, (uint32_t c), (c));

extern "C" void GX__ClearBoundingBox_8016fecc() {
    GXClearBoundingBox();
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 2, 0);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80382b58, GX__ClearBoundingBox_8016fecc, (), ());

// ============================================================================
// FIFO/State Management - No-ops
// ============================================================================

extern "C" void GX__SetDirtyState_8016ee78() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write32(gd + 0x5FCu, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80381bb4, GX__SetDirtyState_8016ee78, (), ());

extern "C" void GX__ResetWriteGatherPipe_8016e6b0() {
    // WPAR reset - not needed on host
}
#if 0
PPC_NATIVE_OVERRIDE_VOID(801c4b30, GX__ResetWriteGatherPipe_8016e6b0, (), ());
#endif
