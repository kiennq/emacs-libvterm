cmake_minimum_required(VERSION 3.11)
include(ExternalProject)

project(emacs-libvterm C)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
   set(LIBVTERM_BUILD_COMMAND "gmake")
else()
   set(LIBVTERM_BUILD_COMMAND "make")
endif()

# Add source files based on platform
if(WIN32)
  set(VTERM_MODULE_SOURCES vterm-module.c utf8.c elisp.c conpty-proxy/arena.c)
else()
  set(VTERM_MODULE_SOURCES vterm-module.c utf8.c elisp.c)
endif()

add_library(vterm-module MODULE ${VTERM_MODULE_SOURCES})
set_target_properties(vterm-module PROPERTIES
  C_STANDARD 99
  C_VISIBILITY_PRESET "hidden"
  POSITION_INDEPENDENT_CODE ON
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

# Profiling option
option(ENABLE_PROFILING "Enable performance profiling instrumentation" OFF)
if(ENABLE_PROFILING)
  target_compile_definitions(vterm-module PRIVATE VTERM_PROFILE)
  message(STATUS "Performance profiling enabled")
endif()

# Set RelWithDebInfo as default build type
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, defaulting to RelWithDebInfo")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type (default RelWithDebInfo)" FORCE)
endif()

# Look for the header file.
option(USE_SYSTEM_LIBVTERM "Use system libvterm instead of the vendored version." ON)

# Try to find the libvterm in system.
if (USE_SYSTEM_LIBVTERM)
  # try to find the vterm.h header file.
  find_path(LIBVTERM_INCLUDE_DIR
    NAMES vterm.h
    )

  # vterm.h is found.
  if (LIBVTERM_INCLUDE_DIR)
    message(STATUS "System libvterm detected")
    execute_process(COMMAND  grep -c "VTermStringFragment" "${LIBVTERM_INCLUDE_DIR}/vterm.h" OUTPUT_VARIABLE VTermStringFragmentExists)
    if (${VTermStringFragmentExists} EQUAL "0")
#    add_compile_definitions(VTermStringFragmentNotExists)
    add_definitions(-DVTermStringFragmentNotExists)
    endif()
    execute_process(COMMAND  grep -c "VTermSelectionMask" "${LIBVTERM_INCLUDE_DIR}/vterm.h" OUTPUT_VARIABLE VTermSelectionMaskExists)
    if (${VTermSelectionMaskExists} EQUAL "0")
#    add_compile_definitions(VTermStringFragmentNotExists)
    add_definitions(-DVTermSelectionMaskNotExists)
    endif()
    execute_process(COMMAND  grep -c "sb_clear" "${LIBVTERM_INCLUDE_DIR}/vterm.h" OUTPUT_VARIABLE VTermSBClearExists)
    if (${VTermSBClearExists} EQUAL "0")
      add_definitions(-DVTermSBClearNotExists)
    endif()
  else()
    message(STATUS "System libvterm not found: libvterm will be downloaded and compiled as part of the build process")
  endif()
endif()

if (LIBVTERM_INCLUDE_DIR)
  find_library(LIBVTERM_LIBRARY NAMES
    vterm
    libvterm
    )

  if(NOT LIBVTERM_LIBRARY)
    message(FATAL_ERROR "libvterm not found")
  endif()
else()
  find_program(LIBTOOL NAMES libtool glibtool)
  if(NOT LIBTOOL)
    message(FATAL_ERROR "libtool not found. Please install libtool")
  endif()

  # libtermiwin is only needed on Windows for terminal compatibility
  if(WIN32)
    # Create a patch script to fix ssize_t typedef conflict on ARM64
    # termiWin hardcodes "typedef int ssize_t" but on ARM64, ssize_t is already defined as __int64
    file(WRITE ${CMAKE_BINARY_DIR}/fix_ssize_t.cmake "
      file(READ \${SOURCE_DIR}/include/termiWin.h TERMIWIN_H_CONTENT)
      string(REPLACE
        \"typedef int ssize_t;        /* common 32 bit case */\"
        \"#ifndef _SSIZE_T_DEFINED\\ntypedef int ssize_t;        /* common 32 bit case */\\n#define _SSIZE_T_DEFINED\\n#endif\"
        TERMIWIN_H_CONTENT \"\${TERMIWIN_H_CONTENT}\")
      file(WRITE \${SOURCE_DIR}/include/termiWin.h \"\${TERMIWIN_H_CONTENT}\")
    ")

    ExternalProject_add(libtermiwin
      GIT_REPOSITORY https://github.com/steeviebops/termiWin
      GIT_TAG 5d5f3474ab823e2c491159cf979ae9fc73f57f9a
      BUILD_BYPRODUCTS <BINARY_DIR>/libtermiwin.a
      PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_BINARY_DIR}/fix_ssize_t.cmake
      INSTALL_COMMAND "")

    ExternalProject_Get_property(libtermiwin SOURCE_DIR)
    ExternalProject_Get_property(libtermiwin BINARY_DIR)

    set(LIBTERMIWIN_INCLUDE_DIR ${SOURCE_DIR}/include)
    set(LIBTERMIWIN_LIBRARY_DIR ${BINARY_DIR})
    set(LIBTERMIWIN_LIBRARY ${BINARY_DIR}/libtermiwin.a)

    # Workaround for https://gitlab.kitware.com/cmake/cmake/issues/15052
    file(MAKE_DIRECTORY ${LIBTERMIWIN_INCLUDE_DIR})

    ExternalProject_add(libvterm
      GIT_REPOSITORY https://github.com/neovim/libvterm.git
      GIT_TAG dfc4c5e5b3dd99247dc95031a8f40087f181dea5
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ${LIBVTERM_BUILD_COMMAND} "CFLAGS=-fPIC -I${LIBTERMIWIN_INCLUDE_DIR}" "LDFLAGS=-static -L${LIBTERMIWIN_LIBRARY_DIR} -ltermiwin"
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS <SOURCE_DIR>/.libs/libvterm.a
      INSTALL_COMMAND "")

    add_dependencies(libvterm libtermiwin)
    add_dependencies(vterm-module libtermiwin)
  else()
    # On non-Windows platforms, build libvterm without termiwin
    ExternalProject_add(libvterm
      GIT_REPOSITORY https://github.com/neovim/libvterm.git
      GIT_TAG dfc4c5e5b3dd99247dc95031a8f40087f181dea5
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ${LIBVTERM_BUILD_COMMAND} "CFLAGS=-fPIC"
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS <SOURCE_DIR>/.libs/libvterm.a
      INSTALL_COMMAND "")

    add_dependencies(vterm-module libvterm)
  endif()

  ExternalProject_Get_property(libvterm SOURCE_DIR)

  set(LIBVTERM_INCLUDE_DIR ${SOURCE_DIR}/include)
  set(LIBVTERM_LIBRARY ${SOURCE_DIR}/.libs/libvterm.a)

  # Workaround for https://gitlab.kitware.com/cmake/cmake/issues/15052
  file(MAKE_DIRECTORY ${LIBVTERM_INCLUDE_DIR})
endif()

# conpty-proxy is no longer needed - ConPTY is handled in-process
# The arena.c from conpty-proxy is still used by vterm-module.c

add_library(vterm STATIC IMPORTED)
set_target_properties(vterm PROPERTIES IMPORTED_LOCATION ${LIBVTERM_LIBRARY})
target_include_directories(vterm INTERFACE ${LIBVTERM_INCLUDE_DIR})

# Link with libvterm (and termiwin on Windows)
if(WIN32)
  add_library(termiwin STATIC IMPORTED)
  set_target_properties(termiwin PROPERTIES IMPORTED_LOCATION ${LIBTERMIWIN_LIBRARY})
  target_include_directories(termiwin INTERFACE ${LIBTERMIWIN_INCLUDE_DIR})
  target_link_libraries(vterm-module PUBLIC vterm termiwin)
else()
  target_link_libraries(vterm-module PUBLIC vterm)
endif()

# Custom run command for testing
add_custom_target(run
  COMMAND emacs -Q -L ${CMAKE_SOURCE_DIR} -L ${CMAKE_BINARY_DIR} --eval "\\(require \\'vterm\\)" --eval "\\(vterm\\)"
  DEPENDS vterm-module
  )
