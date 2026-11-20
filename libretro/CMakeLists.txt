# GBA DS as a libretro core for 游典 (gbads_libretro_android.so): GBA DS's emulation and second-screen
# capture (src/emu.c, mapcap.c, profiles.c, patch.c, logo_masks.c) on libmgba, and libretro/gbads_libretro.c.
#   MGBA_SRC   mGBA source tree (143adf5, scripts/mgba-options.sh)
#   MGBA_BUILD its build dir for this ABI (libmgba.a), configured with scripts/mgba-options.sh
cmake_minimum_required(VERSION 3.18)
project(gbads_libretro C)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_library(mgba STATIC IMPORTED)
set_target_properties(mgba PROPERTIES IMPORTED_LOCATION "${MGBA_BUILD}/libmgba.a")
# the defines libmgba was built with (its headers depend on them)
file(STRINGS "${MGBA_BUILD}/build.ninja" MGBA_DEFINES_LINE REGEX "^  DEFINES = .*-DM_CORE_GBA")
list(GET MGBA_DEFINES_LINE 0 MGBA_DEFINES_LINE)
string(REGEX REPLACE "^  DEFINES = " "" MGBA_DEFINES "${MGBA_DEFINES_LINE}")
separate_arguments(MGBA_DEFINES)
set(S "${ROOT}/src")
add_library(gbads_libretro SHARED
	${ROOT}/libretro/gbads_libretro.c
	${S}/emu.c ${S}/patch.c ${S}/mapcap.c ${S}/profiles.c ${S}/logo_masks.c)
set_target_properties(gbads_libretro PROPERTIES C_STANDARD 11 C_EXTENSIONS ON PREFIX "")
target_compile_options(gbads_libretro PRIVATE ${MGBA_DEFINES} -Wall -Wno-unused-parameter -Wno-missing-field-initializers -fvisibility=default)
target_compile_definitions(gbads_libretro PRIVATE GBADS_VERSION="1.5.6-youdian" GBADS_LIBRETRO)
target_include_directories(gbads_libretro PRIVATE "${MGBA_SRC}/include" "${MGBA_BUILD}/include" "${MGBA_SRC}/src/platform/libretro")
target_link_libraries(gbads_libretro PRIVATE mgba z log m)
target_link_options(gbads_libretro PRIVATE -Wl,--no-undefined)
