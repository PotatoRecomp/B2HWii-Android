// gx_poke.cpp - GXPoke* (direct EFB access) entry points, as no-ops.
#include "gx_internal.h"
#include "runtime_log.h"

#include <dolphin/gx/GXGet.h>

namespace {
void LogOnce(bool& logged, const char* what)
{
    if (!logged) {
        logged = true;
        RT_LOGF(RT_TAG_GX, "%s\n", what);
    }
}
} // namespace

// GXGetCullMode(GXCullMode* mode): Aurora holds the mode GXSetCullMode (gx_pixel.cpp) forwards to it.
// NSMBW reads it while building its warning and message layouts.
extern "C" void GX__GetCullMode_801c5890(uint32_t modePtr) {
    GXCullMode mode = GX_CULL_NONE;
    GXGetCullMode(&mode);
    if (modePtr != 0) {
        Memory::Write32(modePtr, static_cast<uint32_t>(mode));
    }
}
#if 0
PPC_NATIVE_OVERRIDE_VOID(801c5890, GX__GetCullMode_801c5890, (uint32_t modePtr), (modePtr));
#endif

// GXAbortFrame asks the GPU to drop the frame in flight. Aurora submits whole frames and has nothing
// half-finished to abandon, so there is nothing to do.
extern "C" void GX__AbortFrame_801c4ce0() {
    static bool logged = false;
    LogOnce(logged, "GXAbortFrame: no partial GPU frame to abandon (no-op)");
}
PPC_NATIVE_OVERRIDE_VOID(80381578, GX__AbortFrame_801c4ce0, (), ());

// GXPoke*: modes for CPU writes straight into the embedded frame buffer (GXPokeARGB/GXPokeZ). On
// hardware they program pixel-engine registers; the runtime does not emulate CPU-to-EFB access, so
// the modes have no consumer. Accept them (Aurora declares but does not implement these).
#define BASARA_GX_POKE_NOOP(addr, name, params, uses)                                        \
    extern "C" void GX__##name##_##addr params {                                            \
        static bool logged = false;                                                          \
        (void)(uses);                                                                        \
        LogOnce(logged, "GX" #name ": CPU-to-EFB poke modes are not emulated (no-op)");     \
    }

BASARA_GX_POKE_NOOP(801c4fa0, PokeAlphaMode, (uint32_t func, uint32_t threshold), (func, threshold))
PPC_NATIVE_OVERRIDE_VOID(803818d0, GX__PokeAlphaMode_801c4fa0, (uint32_t func, uint32_t threshold), (func, threshold));
BASARA_GX_POKE_NOOP(801c4fb0, PokeAlphaRead, (uint32_t mode), (mode))
PPC_NATIVE_OVERRIDE_VOID(803818e0, GX__PokeAlphaRead_801c4fb0, (uint32_t mode), (mode));
BASARA_GX_POKE_NOOP(801c4fd0, PokeAlphaUpdate, (uint32_t enable), (enable))
// Basara: address checked against the register fields this function writes.
PPC_NATIVE_OVERRIDE_VOID(803818f8, GX__PokeAlphaUpdate_801c4fd0, (uint32_t enable), (enable));
BASARA_GX_POKE_NOOP(801c4ff0, PokeBlendMode, (uint32_t type, uint32_t src, uint32_t dst, uint32_t op), (type, src, dst, op))
PPC_NATIVE_OVERRIDE_VOID(8038190c, GX__PokeBlendMode_801c4ff0, (uint32_t type, uint32_t src, uint32_t dst, uint32_t op),
                         (type, src, dst, op));
BASARA_GX_POKE_NOOP(801c5050, PokeColorUpdate, (uint32_t enable), (enable))
// Basara: address checked against the register fields this function writes.
PPC_NATIVE_OVERRIDE_VOID(80381968, GX__PokeColorUpdate_801c5050, (uint32_t enable), (enable));
BASARA_GX_POKE_NOOP(801c5070, PokeDstAlpha, (uint32_t enable, uint32_t alpha), (enable, alpha))
PPC_NATIVE_OVERRIDE_VOID(8038197c, GX__PokeDstAlpha_801c5070, (uint32_t enable, uint32_t alpha), (enable, alpha));
BASARA_GX_POKE_NOOP(801c5090, PokeDither, (uint32_t dither), (dither))
PPC_NATIVE_OVERRIDE_VOID(80381994, GX__PokeDither_801c5090, (uint32_t dither), (dither));
BASARA_GX_POKE_NOOP(801c50b0, PokeZMode, (uint32_t compareEnable, uint32_t func, uint32_t updateEnable),
                   (compareEnable, func, updateEnable))
PPC_NATIVE_OVERRIDE_VOID(803819a8, GX__PokeZMode_801c50b0, (uint32_t compareEnable, uint32_t func, uint32_t updateEnable),
                         (compareEnable, func, updateEnable));

#undef BASARA_GX_POKE_NOOP
