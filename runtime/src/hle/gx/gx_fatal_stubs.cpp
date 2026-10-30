// Auto-generated GX fatal stubs for NSMBW SMNE01 rev 1 (tools/gen_gx_fatal_stubs.py)
#include "hle_stubs.h"
#include "runtime_log.h"

namespace {
[[noreturn]] void HaltGX(uint32_t addr, const char* name) {
    const char* symbol = name ? name : "<unknown GX symbol>";
    RT_LOGF(RT_TAG_GX,
            "unimplemented GX entry point: %s at guest address 0x%08X.\n"
            "[gx] This graphics call has no Aurora implementation bound to it yet, so the\n"
            "[gx] runtime cannot continue without silently dropping GPU state. Bind it in\n"
            "[gx] runtime/src/hle/gx/ and remove the stub from gx_fatal_stubs.cpp.\n",
            symbol, addr);
    std::fflush(stderr);
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "%s at guest address 0x%08X has no Aurora implementation bound to it, so the "
                  "runtime stopped rather than keep rendering with missing GPU state.",
                  symbol, addr);
    ShowRuntimeFatalPopup("the game called an unimplemented graphics function", message);
    std::abort();
}
} // namespace

// Every fatal stub is the same two statements with the address and the symbol
// name substituted, so the body comes from this macro. The registration is
// deliberately still spelled out per entry so the translator's runtime-native
// index sees the literal PPC_NATIVE_OVERRIDE_VOID invocation. Hiding it inside
// this macro would leave the index unable to associate an address with the stub.
#define GX_FATAL_STUB(addr, sym) \
    extern "C" void gx_stub_##addr(CpuContext* ctx) { (void)ctx; HaltGX(0x##addr, sym); }

#if 0
GX_FATAL_STUB(801c19f0, "__GXDefaultTexRegionCallback_801c19f0") PPC_NATIVE_OVERRIDE_VOID(801c19f0, gx_stub_801c19f0, (CpuContext* ctx), (ctx));
#endif
#if 0
GX_FATAL_STUB(801c1ae0, "__GXDefaultTlutRegionCallback_801c1ae0") PPC_NATIVE_OVERRIDE_VOID(801c1ae0, gx_stub_801c1ae0, (CpuContext* ctx), (ctx));
#endif
GX_FATAL_STUB(8037eaf0, "__GXShutdown_801c1b10") PPC_NATIVE_OVERRIDE_VOID(8037eaf0, gx_stub_8037eaf0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8037fba0, "GXCPInterruptHandler_801c2b80") PPC_NATIVE_OVERRIDE_VOID(8037fba0, gx_stub_8037fba0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8038064c, "__GXCleanGPFifo_801c35c0") PPC_NATIVE_OVERRIDE_VOID(8038064c, gx_stub_8038064c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80380a9c, "__GXSetVCD_801c3c40") PPC_NATIVE_OVERRIDE_VOID(80380a9c, gx_stub_80380a9c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80380b4c, "__GXCalculateVLim_801c3cf0") PPC_NATIVE_OVERRIDE_VOID(80380b4c, gx_stub_80380b4c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80381010, "__GXSetVAT_801c4420") PPC_NATIVE_OVERRIDE_VOID(80381010, gx_stub_80381010, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80381414, "__GXAbort_801c4b70") PPC_NATIVE_OVERRIDE_VOID(80381414, gx_stub_80381414, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80381f78, "__GXSendFlushPrim_801c56c0") PPC_NATIVE_OVERRIDE_VOID(80381f78, gx_stub_80381f78, (CpuContext* ctx), (ctx));
#if 0
GX_FATAL_STUB(801c58f0, "__GXSetGenMode_801c58f0") PPC_NATIVE_OVERRIDE_VOID(801c58f0, gx_stub_801c58f0, (CpuContext* ctx), (ctx));
#endif
GX_FATAL_STUB(803850b8, "__GXSetProjection_801c97f0") PPC_NATIVE_OVERRIDE_VOID(803850b8, gx_stub_803850b8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80385368, "__GXSetViewport_801c9b40") PPC_NATIVE_OVERRIDE_VOID(80385368, gx_stub_80385368, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80385540, "__GXSetMatrixIndex_801c9d90") PPC_NATIVE_OVERRIDE_VOID(80385540, gx_stub_80385540, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(803855c8, "GXSetGPMetric_801c9e20") PPC_NATIVE_OVERRIDE_VOID(803855c8, gx_stub_803855c8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80385df4, "GXClearGPMetric_801ca640") PPC_NATIVE_OVERRIDE_VOID(80385df4, gx_stub_80385df4, (CpuContext* ctx), (ctx));
