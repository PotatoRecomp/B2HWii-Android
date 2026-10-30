// strcmp (MSL, 0x802E1AB4) natively. Loading a scene's effects (nw4r::ef::Resource::RelocateCommand)
// looks every emitter's textures and children up by name, comparing thousands of strings in the one
// frame that loads them: about a quarter of the course intro's slow load frame on a Pixel 3 XL, a
// translated byte loop. The result is the guest's exactly: the difference of the first two bytes
// that differ, as unsigned values (its word loop only finds them faster), or 0 at a shared NUL.
#include "hle_stubs.h"
#include "memory.h"

#include <cstdint>

namespace {

// Compared in windows the guest map vouches for whole; a string that runs into an unmapped page
// goes byte by byte, so the guest's own access violation is what it hits.
constexpr uint32_t kWindow = 64;

int32_t CompareSlow(uint32_t a, uint32_t b) {
    while (true) {
        const uint8_t ca = Memory::Read8(a++);
        const uint8_t cb = Memory::Read8(b++);
        if (ca != cb) {
            return static_cast<int32_t>(ca) - static_cast<int32_t>(cb);
        }
        if (ca == 0) {
            return 0;
        }
    }
}

} // namespace

extern "C" int32_t Strcmp_HLE_802e1ab4(uint32_t a, uint32_t b)
{
    while (true) {
        // The inline page-table lookup: effect loading makes thousands of these calls on short names.
        uint8_t* pa = nullptr;
        uint8_t* pb = nullptr;
        if (!MemoryInline::TryGetPointerFast(a, kWindow, pa) || !MemoryInline::TryGetPointerFast(b, kWindow, pb)) {
            return CompareSlow(a, b);
        }
        for (uint32_t i = 0; i < kWindow; ++i) {
            if (pa[i] != pb[i]) {
                return static_cast<int32_t>(pa[i]) - static_cast<int32_t>(pb[i]);
            }
            if (pa[i] == 0) {
                return 0;
            }
        }
        a += kWindow;
        b += kWindow;
    }
}
PPC_NATIVE_OVERRIDE(8038F228, Strcmp_HLE_802e1ab4, int32_t, (uint32_t a, uint32_t b), (a, b));

// nw4r::ef::breffSearchName (0x80294CA0) and breftSearchName (0x80294E90), instruction for
// instruction the same: the first entry of an effect or texture project's name table whose name
// equals `name`, as project-table base + the entry's offset, or 0. Relocating a scene's effects
// calls them for every emitter; translated, each call paid a dispatch per strcmp, three times the
// compare itself, and the lookups were two thirds of the course intro's slowest frame on a Pixel 3 XL.
// Table: at project + *(u32*)project, a u16 count at +4, then per entry a big-endian u16 name
// length, the name (NUL included), and 8 bytes whose first big-endian word is the offset.
namespace {
uint32_t SearchEffectNameTable(uint32_t project, uint32_t name) {
    const uint32_t base = project + Memory::Read32(project);
    const uint32_t count = Memory::Read16(base + 4);
    uint32_t entry = base + 8;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t nameLength = Memory::Read16(entry);
        const uint32_t entryName = entry + 2;
        const uint32_t offset = Memory::Read32(entryName + nameLength);
        entry = entryName + nameLength + 8;
        if (Strcmp_HLE_802e1ab4(name, entryName) == 0) {
            return base + offset;
        }
    }
    return 0;
}
} // namespace

extern "C" uint32_t BreffSearchName_HLE_80294ca0(uint32_t project, uint32_t name)
{
    return SearchEffectNameTable(project, name);
}
#if 0
PPC_NATIVE_OVERRIDE(80294CA0, BreffSearchName_HLE_80294ca0, uint32_t, (uint32_t project, uint32_t name), (project, name));
#endif

extern "C" uint32_t BreftSearchName_HLE_80294e90(uint32_t project, uint32_t name)
{
    return SearchEffectNameTable(project, name);
}
#if 0
PPC_NATIVE_OVERRIDE(80294E90, BreftSearchName_HLE_80294e90, uint32_t, (uint32_t project, uint32_t name), (project, name));
#endif
