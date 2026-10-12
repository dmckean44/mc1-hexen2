cmake_minimum_required(VERSION 3.10)

project(mc1-hexen2 LANGUAGES C ASM)

set(SRC_NAMES
    d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite d_surf d_vars d_zpoint
    r_aclip r_alias r_bsp r_draw r_edge r_efrag r_light r_main r_misc r_part r_sky r_sprite
    r_surf r_vars screen draw model
    snd_null bgmnull cd_null midi_nul
    net_loop net_main
    chase cl_demo cl_effect cl_inlude cl_input cl_main cl_parse cl_string cl_tent cl_cmd
    console keys menu sbar view wad cmd q_endian link_ops sizebuf strlcat strlcpy qsnprint
    msg_io common debuglog quakefs crc cvar cfgfile host host_cmd host_string mathlib
    pr_cmds pr_edict pr_exec sv_effect sv_main sv_move sv_phys sv_user world zone hashindex
)

set(SRC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../h2shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

foreach(name ${SRC_NAMES})
  unset(path CACHE)
  find_file(path ${name}.c PATHS ${SRC_DIRS} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  if(NOT path)
    message(FATAL_ERROR "${name}.c not found")
  endif()
  list(APPEND SRCS ${path})
endforeach()
unset(path CACHE)

option(MR32_ASM "Use the MRISC32 assembler routines" ON)

list(APPEND DEFS -DDEMOBUILD -D_NO_SOUND -D_NO_MIDIDRV -D_NO_CDAUDIO -DNDEBUG=1)

if(MC1)
  list(APPEND SRCS sys_mc1.c net_mc1.c fastmath_mc1.c ../h2shared/d_pe.c ../h2shared/vid_mc1.c ../h2shared/in_mc1.c)
  if(MR32_ASM)
    list(APPEND SRCS ../h2shared/d_scan_mr32.s)
  else()
    list(APPEND DEFS -DNO_MR32_ASM)
  endif()
  list(APPEND DEFS -DMC1)
  list(APPEND OPTS -mcmodel=small -flto -fsingle-precision-constant)
  list(APPEND LIBS m -flto)
  if(MC1SDK_HOME)
    message("Building for running on an MC1 computer.")
    list(APPEND DEFS -DMC1_SDK)
    list(APPEND OPTS "-I${MC1SDK_HOME}/include")
    list(APPEND LIBS "-L${MC1SDK_HOME}/lib" -mno-crt0 -lmc1crt0-app -lmc1 "-T app-xram.ld")
  else()
    message("Building for running in the MRISC32 simulator.")
    list(APPEND LIBS -msim)
  endif()
else()
  message(FATAL_ERROR "Use the Makefile for host builds.")
endif()

# The functions that run most per byte go first in .text so they share the instruction cache well
# (../../mc1/hot_functions.txt, made by mc1/hotlist.py from an mr32sim perf log).
set(HOTLIST ${CMAKE_CURRENT_SOURCE_DIR}/../../mc1/hot_functions.txt)
if(MC1 AND EXISTS ${HOTLIST})
  if(MC1SDK_HOME)
    file(READ "${MC1SDK_HOME}/lib/app-xram.ld" LDSCRIPT)
    list(REMOVE_ITEM LIBS "-T app-xram.ld")
  else()
    execute_process(COMMAND ${CMAKE_C_COMPILER} -msim -Wl,--verbose -x c /dev/null -o /dev/null
                    OUTPUT_VARIABLE LDVERBOSE ERROR_QUIET)
    string(REGEX REPLACE ".*\n=+\n(.*)\n=+\n.*" "\\1" LDSCRIPT "${LDVERBOSE}")
  endif()
  file(STRINGS ${HOTLIST} HOTFUNCS)
  set(HOTSECTIONS "")
  foreach(f ${HOTFUNCS})
    string(APPEND HOTSECTIONS "    *(.text.${f})\n")
  endforeach()
  string(REPLACE "*(.text.start)" "*(.text.start)\n${HOTSECTIONS}" LDSCRIPT "${LDSCRIPT}")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hot.ld "${LDSCRIPT}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HOTLIST})
  list(APPEND OPTS -ffunction-sections)
  list(APPEND LIBS -ffunction-sections "-T ${CMAKE_CURRENT_BINARY_DIR}/hot.ld")
endif()

add_executable(mc1hexen2 ${SRCS})
target_include_directories(mc1hexen2 PRIVATE . ../h2shared ../../common)
target_compile_definitions(mc1hexen2 PRIVATE ${DEFS})
target_compile_options(mc1hexen2 PRIVATE ${OPTS} -ffast-math -Wall -Wno-unused-variable -Wno-unused-but-set-variable)
target_link_libraries(mc1hexen2 PRIVATE ${LIBS})
