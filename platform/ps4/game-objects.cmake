# This target deliberately has no stub renderer or fake game entry point. It
# checks the actual upstream game manifest against the Orbis ABI while the
# platform and Dawn libraries are being ported. It is not a linked game.
set(PRIME_GAME "${PROJECT_SOURCE_DIR}/game/MetroidPrimePort")
include("${PRIME_GAME}/files.cmake")
list(REMOVE_ITEM MP_GAME_SOURCES src/NESemu/modwrapper.cpp src/MetroidPrime/TypesMatch.cpp)
list(FILTER MP_GAME_SOURCES EXCLUDE REGEX "^src/NESemu/")
list(APPEND MP_GAME_SOURCES src/MetroidPrime/main.cpp
  platform/port_remastered_movers.cpp platform/port_raster_font.cpp
  platform/port_morph_ball.cpp platform/port_automapper.cpp
  platform/port_particle_vfx_read.cpp platform/port_element_gen.cpp
  platform/port_entity_schema.cpp src/MetroidPrime/CPortNestedLights.cpp)
list(TRANSFORM MP_GAME_SOURCES PREPEND "${PRIME_GAME}/")
add_library(prime_game_objects OBJECT ${MP_GAME_SOURCES})
target_compile_definitions(prime_game_objects PRIVATE TARGET_PC AURORA AURORA_ENABLE_GX
  VERSION=0 MUSY_TARGET=MUSY_TARGET_PC)
target_include_directories(prime_game_objects PRIVATE
  "${PRIME_GAME}/platform/include" "${PRIME_GAME}/include"
  "${PRIME_GAME}/extern/aurora/include" "${PRIME_GAME}/extern/musyx/include"
  "${PROJECT_SOURCE_DIR}/external/SDL-release-3.4.10/include")
target_compile_options(prime_game_objects PRIVATE
  -Wformat -Werror=return-type -Wno-narrowing -Wno-multichar -Wno-write-strings
  -include "${PRIME_GAME}/platform/compat.h")
