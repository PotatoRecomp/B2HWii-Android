# Android: aurora's own WebGPU-on-Vulkan backend (lib/vulkan/vk_webgpu.cpp, from the SB2H-NWiiRecomp
# port, GPLv3) next to Dawn in the same libmain, chosen at run time (lib/webgpu/gpu.cpp initialize).
#   - Dawn's archive is copied with its wgpu* functions renamed to dawn_wgpu* (dawn_wgpu_syms.txt),
#   - vk_webgpu.cpp is compiled with vkw_rename.h (its functions become vkw_wgpu*),
#   - wgpu_dispatch.cpp defines wgpu* and calls one or the other.
# The three generated files come from tools/b-wgpu-dispatch.py (run it again after a Dawn update).
# Tint (WGSL -> SPIR-V) is taken from the Dawn archive; its headers are in extern/tint.
find_package(Vulkan REQUIRED)

include(FetchContent)
# The header alone (SOURCE_SUBDIR none: VMA's own CMake project is not added).
set(_aurora_vma_local "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/src/VulkanMemoryAllocator")
if (EXISTS "${_aurora_vma_local}/include/vk_mem_alloc.h")
  FetchContent_Declare(vma SOURCE_DIR "${_aurora_vma_local}" SOURCE_SUBDIR none)
else ()
  FetchContent_Declare(vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
                       GIT_TAG v3.1.0 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
endif ()
FetchContent_MakeAvailable(vma)

# Dawn with renamed WebGPU entry points, made once at configure time from the prebuilt archive.
get_target_property(_aurora_dawn_archive dawn::webgpu_dawn IMPORTED_LOCATION_RELEASE)
if (NOT _aurora_dawn_archive)
  get_target_property(_aurora_dawn_archive dawn::webgpu_dawn IMPORTED_LOCATION)
endif ()
set(_aurora_dawn_wgpu_syms "${CMAKE_CURRENT_SOURCE_DIR}/lib/vulkan/dawn_wgpu_syms.txt")
# Dawn's own dlopen/dlclose (of libvulkan.so) go to lib/webgpu/custom_driver.cpp, which can hand it a
# player-installed driver. Only Dawn's calls: a global --wrap would also catch linkernsbypass, which
# walks the code of &dlopen to find the linker.
set(_aurora_dawn_syms "${CMAKE_BINARY_DIR}/dawn_dispatch_syms.txt")
file(READ "${_aurora_dawn_wgpu_syms}" _aurora_dawn_syms_text)
string(APPEND _aurora_dawn_syms_text "dlopen aurora_dawn_dlopen
dlclose aurora_dawn_dlclose
")
file(WRITE "${_aurora_dawn_syms}.new" "${_aurora_dawn_syms_text}")
file(COPY_FILE "${_aurora_dawn_syms}.new" "${_aurora_dawn_syms}" ONLY_IF_DIFFERENT)
set(_aurora_dawn_renamed "${CMAKE_BINARY_DIR}/libwebgpu_dawn_dispatch.a")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_aurora_dawn_wgpu_syms}" "${_aurora_dawn_archive}")
if (NOT EXISTS "${_aurora_dawn_renamed}" OR "${_aurora_dawn_archive}" IS_NEWER_THAN "${_aurora_dawn_renamed}"
    OR "${_aurora_dawn_syms}" IS_NEWER_THAN "${_aurora_dawn_renamed}")
  execute_process(COMMAND "${CMAKE_OBJCOPY}" "--redefine-syms=${_aurora_dawn_syms}" "${_aurora_dawn_archive}"
                          "${_aurora_dawn_renamed}"
                  RESULT_VARIABLE _aurora_objcopy_result)
  if (NOT _aurora_objcopy_result EQUAL 0)
    message(FATAL_ERROR "aurora: renaming Dawn's WebGPU functions failed (${CMAKE_OBJCOPY})")
  endif ()
endif ()
set_target_properties(dawn::webgpu_dawn PROPERTIES IMPORTED_LOCATION_RELEASE "${_aurora_dawn_renamed}"
                                                   IMPORTED_LOCATION "${_aurora_dawn_renamed}")

add_library(aurora_vk STATIC
        lib/vulkan/vk_webgpu.cpp
        lib/vulkan/vma_impl.cpp
        lib/vulkan/wgpu_dispatch.cpp
)
set_target_properties(aurora_vk PROPERTIES FOLDER "aurora")
set_source_files_properties(lib/vulkan/vk_webgpu.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/lib/vulkan/vkw_rename.h")
target_include_directories(aurora_vk PRIVATE ${vma_SOURCE_DIR}/include)
target_include_directories(aurora_vk SYSTEM PRIVATE extern/tint extern/tint/include)
target_compile_definitions(aurora_vk PRIVATE
        TINT_BUILD_WGSL_READER=1 TINT_BUILD_SPV_WRITER=1 TINT_BUILD_SPV_READER=0 TINT_BUILD_WGSL_WRITER=0
        TINT_BUILD_MSL_WRITER=0 TINT_BUILD_HLSL_WRITER=0 TINT_BUILD_GLSL_WRITER=0 TINT_BUILD_GLSL_VALIDATOR=0
        TINT_BUILD_IS_LINUX=1 TINT_BUILD_IS_WIN=0 TINT_BUILD_IS_MAC=0)
target_link_libraries(aurora_vk PRIVATE Vulkan::Vulkan dawn::dawn_public_config dawn::webgpu_dawn)
target_compile_definitions(aurora_core PRIVATE AURORA_VULKAN_BACKEND)
