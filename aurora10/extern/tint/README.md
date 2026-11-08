# Tint headers

The headers the Vulkan backend (`lib/vulkan/vk_webgpu.cpp`) needs to call the WGSL reader and the
SPIR-V writer of Tint. Tint itself is linked from the prebuilt Dawn archive, so these headers must
come from the same Dawn commit as that archive: `google/dawn` 13abc3bc8ea2d3c2050f9e77a12d012108ceee24,
which `AURORA_DAWN_VERSION` v20260603.191052 is built from. Update both together.

Only the files the backend includes, directly or indirectly, are kept. To refresh them, check out that
commit and copy the files `clang++ -MM` lists for `vk_webgpu.cpp`'s Tint includes. License: LICENSE
(BSD-3-Clause).
