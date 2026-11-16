if(NOT PRIME_SDK_BUNDLE)
  if(DEFINED ENV{PRIME_SDK_BUNDLE})
    set(PRIME_SDK_BUNDLE "$ENV{PRIME_SDK_BUNDLE}" CACHE PATH "Pinned OpenOrbis/Mesa bundle")
  else()
    message(FATAL_ERROR "Set PRIME_SDK_BUNDLE to the pinned native SDK bundle")
  endif()
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PRIME_SDK_BUNDLE)
include("${PRIME_SDK_BUNDLE}/toolchain/orbis-sdk.cmake")
# The SDK script omits priority constructors such as libc++19's .init_array.100.
# Keep the shared SDK read-only and use a project-local corrected derivative.
foreach(prime_link_flags CMAKE_EXE_LINKER_FLAGS_INIT CMAKE_EXE_LINKER_FLAGS)
  if(DEFINED ${prime_link_flags})
    string(REPLACE "${ORBIS_COMPAT_DIR}/cmake/orbis-tls.ld"
      "${CMAKE_CURRENT_LIST_DIR}/orbis-native.ld"
      ${prime_link_flags} "${${prime_link_flags}}")
    # create-fself generates the native interpreter header itself. An incoming
    # FreeBSD interpreter would otherwise survive and produce two PT_INTERPs.
    if(NOT "${${prime_link_flags}}" MATCHES "--no-dynamic-linker")
      string(APPEND ${prime_link_flags} " -Wl,--no-dynamic-linker")
    endif()
  endif()
endforeach()
# Orbis uses ELF and lld. Generic CMake platforms do not advertise archive
# groups automatically, even though the native linker supports them.
set(CMAKE_CXX_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)
# Jaguar scheduling without AVX: the Orbis application context does not enable
# AVX. The game also depends on signed plain char for audio stream sentinels.
string(APPEND CMAKE_C_FLAGS_INIT " --sysroot=${OO_PS4_TOOLCHAIN} -msse4.2 -mpopcnt -mcx16 -mtune=btver2 -mno-avx -fsigned-char")
string(APPEND CMAKE_CXX_FLAGS_INIT " --sysroot=${OO_PS4_TOOLCHAIN} -msse4.2 -mpopcnt -mcx16 -mtune=btver2 -mno-avx -fsigned-char")
