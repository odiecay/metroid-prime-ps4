cmake_minimum_required(VERSION 3.25)
project(prime_native_game LANGUAGES C CXX)
if(NOT PS4 OR NOT PRIME_CXX_PREFIX)
  message(FATAL_ERROR "Use the modern native Orbis toolchain")
endif()
get_filename_component(PRIME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Aurora uses zstd compression/decompression for its pipeline cache. It never
# trains dictionaries. Keep the unused host dictionary training/tools out of
# the console library; no qsort_r ABI is available in this native libc.
set(ZSTD_BUILD_DICTBUILDER OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
include(FetchContent)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# Same immutable archive as Aurora. Its FreeBSD VDSO path is inapplicable to
# native Orbis/musl. FetchContent's first declaration supplies a checked patch.
FetchContent_Declare(abseil-cpp
  URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz
  URL_HASH SHA256=f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PATCH_COMMAND "${Python3_EXECUTABLE}" "${PRIME_ROOT}/tools/patch_dependency.py"
    <SOURCE_DIR> "${PRIME_ROOT}/patches/aurora-abseil-orbis.patch"
  EXCLUDE_FROM_ALL)
FetchContent_Declare(tracy
  URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.zip
  URL_HASH SHA256=908f3a2917fa86a247abfcf85dcf04bad1db6986a4d40f94b70512f3e9e98d5b
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PATCH_COMMAND "${Python3_EXECUTABLE}" "${PRIME_ROOT}/tools/patch_dependency.py"
    <SOURCE_DIR> "${PRIME_ROOT}/patches/aurora-tracy-orbis.patch"
  EXCLUDE_FROM_ALL)
set(MP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(MP_BUNDLED_DEPS ON CACHE BOOL "" FORCE)
set(AURORA_BUNDLED_DEPS ON CACHE BOOL "" FORCE)
set(MP_USE_MOLD OFF CACHE BOOL "" FORCE)
set(MP_USE_CCACHE OFF CACHE BOOL "" FORCE)
set(AURORA_DAWN_PROVIDER system CACHE STRING "" FORCE)
set(AURORA_DAWN_LINKAGE static CACHE STRING "" FORCE)
set(AURORA_SDL3_PROVIDER system CACHE STRING "" FORCE)
set(AURORA_SDL3_LINKAGE static CACHE STRING "" FORCE)
set(AURORA_NOD_PROVIDER vendor CACHE STRING "" FORCE)
# Native packages only. Optional TLS/import extensions are not satisfied with
# host OpenSSL libraries. They can be enabled when a native build is provided.
set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig TRUE)
add_subdirectory("${PRIME_ROOT}/platform/ps4/sdl" native-sdl)
set(SDL3_FOUND TRUE)

add_library(prime_gc_iso STATIC "${PRIME_ROOT}/platform/ps4/disc/gc_iso.cpp")
target_include_directories(prime_gc_iso PUBLIC "${PRIME_ROOT}/external/nod-host-package/include")
add_library(nod::nod ALIAS prime_gc_iso)

# This is the native Dawn archive built by tools/build_dawn.py. A missing or
# failed Dawn build remains a link failure; there is no renderer stub.
add_library(webgpu_dawn STATIC IMPORTED GLOBAL)
set_target_properties(webgpu_dawn PROPERTIES
  IMPORTED_LOCATION "${PRIME_ROOT}/build/dawn-modern/src/dawn/native/libwebgpu_dawn.a"
  INTERFACE_INCLUDE_DIRECTORIES "${PRIME_ROOT}/external/dawn/include;${PRIME_ROOT}/build/dawn-modern/gen/include")
add_library(dawn::webgpu_dawn ALIAS webgpu_dawn)
add_library(prime_gpu_wait STATIC IMPORTED GLOBAL)
set_target_properties(prime_gpu_wait PROPERTIES
  IMPORTED_LOCATION "${PRIME_ROOT}/build/dawn-bridge/libprime_gpu_wait.a")
set(Dawn_FOUND TRUE)
add_subdirectory("${ORBIS_COMPAT_DIR}/vkloader" native-vkloader)
# Replace only this project's driver link input. The shared SDK stays read-only.
set(prime_radv_memory_archive "${PRIME_ROOT}/build/mesa-native-memory/libvulkan_radeon.a")
if(NOT EXISTS "${prime_radv_memory_archive}")
  message(FATAL_ERROR "Run tools/build_mesa_overlay.py before configuring the game")
endif()
get_target_property(prime_vk_libraries ps4-vkloader INTERFACE_LINK_LIBRARIES)
set(prime_sdk_radv_archive "${ORBIS_MESA_BUILD}/src/amd/vulkan/libvulkan_radeon.a")
list(FIND prime_vk_libraries "${prime_sdk_radv_archive}" prime_radv_index)
if(prime_radv_index LESS 0)
  message(FATAL_ERROR "Native loader's RADV input changed; review its dependency contract")
endif()
string(REPLACE "${prime_sdk_radv_archive}" "${prime_radv_memory_archive}" prime_vk_libraries "${prime_vk_libraries}")
set_property(TARGET ps4-vkloader PROPERTY INTERFACE_LINK_LIBRARIES "${prime_vk_libraries}")
add_subdirectory(MetroidPrimePort upstream)
# Linker scripts affect TLS relocations as well as segment layout. Ninja must
# relink when this project-owned script changes, even if no C++ source did.
set_property(TARGET metroid_prime_port APPEND PROPERTY LINK_DEPENDS
  "${PRIME_ROOT}/platform/ps4/orbis-native.ld")
target_include_directories(metroid_prime_port PRIVATE "${PRIME_ROOT}/platform/ps4")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/MetroidPrimePort/src/MetroidPrime/CStateManager.cpp"
  TARGET_DIRECTORY mp_game APPEND PROPERTY INCLUDE_DIRECTORIES "${PRIME_ROOT}/platform/ps4")
# zstd's optional weak tracing hooks have no native provider and cannot be
# assigned to an Orbis import library. Compression itself stays fully enabled.
target_compile_definitions(libzstd_static PRIVATE ZSTD_TRACE=0)
target_compile_options(sqlite3 PRIVATE
  -include "${PRIME_ROOT}/platform/ps4/compat/sqlite_orbis_fcntl.h")
get_target_property(prime_game_sources metroid_prime_port SOURCES)
list(FILTER prime_game_sources EXCLUDE REGEX "(^|/)platform/port_(crash|log_file)\\.cpp$")
set_property(TARGET metroid_prime_port PROPERTY SOURCES "${prime_game_sources}")
target_sources(metroid_prime_port PRIVATE
  "${PRIME_ROOT}/platform/ps4/startup.c"
  "${PRIME_ROOT}/platform/ps4/stdio.c"
  "${PRIME_ROOT}/platform/ps4/sqlite.c"
  "${PRIME_ROOT}/platform/ps4/graphics_profile.c"
  "${PRIME_ROOT}/platform/ps4/gameplay_policy.c"
  "${PRIME_ROOT}/platform/ps4/lifecycle.cpp"
  "${PRIME_ROOT}/platform/ps4/frame_metrics.cpp"
  "${PRIME_ROOT}/platform/ps4/frame_trace_writer.cpp"
  "${PRIME_ROOT}/platform/ps4/port_log.cpp"
  "${PRIME_ROOT}/platform/ps4/video.c"
  "${PRIME_ROOT}/platform/ps4/video_profile.c"
  "${PRIME_ROOT}/platform/ps4/entry.cpp"
  "${PRIME_ROOT}/platform/ps4/candidate_profile.cpp"
  "${PRIME_ROOT}/platform/ps4/benchmark_tour.cpp"
  "${PRIME_ROOT}/platform/ps4/asset_bundle.cpp"
  "${PRIME_ROOT}/platform/ps4/crash.cpp"
  "${PRIME_ROOT}/platform/ps4/aurora_bridge.cpp"
  "${PRIME_ROOT}/platform/ps4/menu_console_model.cpp")
target_link_libraries(metroid_prime_port PRIVATE prime_pad ps4-vkloader sqlite3 prime_gpu_wait
  SceSysmodule SceUserService SceSystemService SceCommonDialog SceMsgDialog SceNet)
target_link_options(metroid_prime_port PRIVATE
  "LINKER:--wrap=__dup3" "LINKER:--wrap=_write" "LINKER:--wrap=_writev"
  "LINKER:--wrap=sceVideoOutClose" "LINKER:--wrap=sceVideoOutSubmitFlip")
