#
# Copyright 2017 The boltdb Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include(CMakeParseArguments)
include(boltdbConfigureCopts)
include(boltdbDll)

# The IDE folder for boltdb that will be used if boltdb is included in a CMake
# project that sets
#    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# For example, Visual Studio supports folders.
if(NOT DEFINED BOLTDB_IDE_FOLDER)
  set(BOLTDB_IDE_FOLDER boltdb)
endif()

if(BOLTDB_USE_SYSTEM_INCLUDES)
  set(BOLTDB_INTERNAL_INCLUDE_WARNING_GUARD SYSTEM)
else()
  set(BOLTDB_INTERNAL_INCLUDE_WARNING_GUARD "")
endif()

# boltdb_cc_library()
#
# CMake function to imitate Bazel's cc_library rule.
#
# Parameters:
# NAME: name of target (see Note)
# HDRS: List of public header files for the library
# SRCS: List of source files for the library
# DEPS: List of other libraries to be linked in to the binary targets
# COPTS: List of private compile options
# DEFINES: List of public defines
# LINKOPTS: List of link options
# PUBLIC: Add this so that this library will be exported under boltdb::
# Also in IDE, target will appear in boltdb folder while non PUBLIC will be in boltdb/internal.
# TESTONLY: When added, this target will only be built if both
#           BUILD_TESTING=ON and BOLTDB_BUILD_TESTING=ON.
#
# Note:
# By default, boltdb_cc_library will always create a library named boltdb_${NAME},
# and alias target boltdb::${NAME}.  The boltdb:: form should always be used.
# This is to reduce namespace pollution.
#
# boltdb_cc_library(
#   NAME
#     awesome
#   HDRS
#     "a.h"
#   SRCS
#     "a.cc"
# )
# boltdb_cc_library(
#   NAME
#     fantastic_lib
#   SRCS
#     "b.cc"
#   DEPS
#     boltdb::awesome # not "awesome" !
#   PUBLIC
# )
#
# boltdb_cc_library(
#   NAME
#     main_lib
#   ...
#   DEPS
#     boltdb::fantastic_lib
# )
#
# TODO(b/320467376): Implement "ALWAYSLINK".
function(boltdb_cc_library)
  cmake_parse_arguments(BOLTDB_CC_LIB
    "DISABLE_INSTALL;PUBLIC;TESTONLY"
    "NAME"
    "HDRS;SRCS;COPTS;DEFINES;LINKOPTS;DEPS"
    ${ARGN}
  )

  if(BOLTDB_CC_LIB_TESTONLY AND
      NOT ((BUILD_TESTING AND BOLTDB_BUILD_TESTING) OR
        (BOLTDB_BUILD_TEST_HELPERS AND BOLTDB_CC_LIB_PUBLIC)))
    return()
  endif()

  if(BOLTDB_ENABLE_INSTALL)
    set(_NAME "${BOLTDB_CC_LIB_NAME}")
  else()
    set(_NAME "boltdb_${BOLTDB_CC_LIB_NAME}")
  endif()

  # Check if this is a header-only library
  # Note that as of February 2019, many popular OS's (for example, Ubuntu
  # 16.04 LTS) only come with cmake 3.5 by default.  For this reason, we can't
  # use list(FILTER...)
  set(BOLTDB_CC_SRCS "${BOLTDB_CC_LIB_SRCS}")
  foreach(src_file IN LISTS BOLTDB_CC_SRCS)
    if(${src_file} MATCHES ".*\.(h|inc)")
      list(REMOVE_ITEM BOLTDB_CC_SRCS "${src_file}")
    endif()
  endforeach()

  if(BOLTDB_CC_SRCS STREQUAL "")
    set(BOLTDB_CC_LIB_IS_INTERFACE 1)
  else()
    set(BOLTDB_CC_LIB_IS_INTERFACE 0)
  endif()

  # Determine this build target's relationship to the DLL. It's one of four things:
  # 1. "dll"     -- This target is part of the DLL
  # 2. "dll_dep" -- This target is not part of the DLL, but depends on the DLL.
  #                 Note that we assume any target not in the DLL depends on the
  #                 DLL. This is not a technical necessity but a convenience
  #                 which happens to be true, because nearly every target is
  #                 part of the DLL.
  # 3. "shared"  -- This is a shared library, perhaps on a non-windows platform
  #                 where DLL doesn't make sense.
  # 4. "static"  -- This target does not depend on the DLL and should be built
  #                 statically.
  if (${BOLTDB_BUILD_DLL})
    if(BOLTDB_ENABLE_INSTALL)
      boltdb_internal_dll_contains(TARGET ${_NAME} OUTPUT _in_dll)
      boltdb_internal_test_dll_contains(TARGET ${_NAME} OUTPUT _in_test_dll)
    else()
      boltdb_internal_dll_contains(TARGET ${BOLTDB_CC_LIB_NAME} OUTPUT _in_dll)
      boltdb_internal_test_dll_contains(TARGET ${BOLTDB_CC_LIB_NAME} OUTPUT _in_test_dll)
    endif()
    if (${_in_dll} OR ${_in_test_dll})
      # This target should be replaced by the DLL
      set(_build_type "dll")
      set(BOLTDB_CC_LIB_IS_INTERFACE 1)
    else()
      # Building a DLL, but this target is not part of the DLL
      set(_build_type "dll_dep")
    endif()
  elseif(BUILD_SHARED_LIBS)
    set(_build_type "shared")
  else()
    set(_build_type "static")
  endif()

  # Generate a pkg-config file for every library:
  if(BOLTDB_ENABLE_INSTALL)
    if(boltdb_VERSION)
      set(PC_VERSION "${boltdb_VERSION}")
    else()
      set(PC_VERSION "head")
    endif()
    if(NOT _build_type STREQUAL "dll")
      set(LNK_LIB "${LNK_LIB} -lboltdb_${_NAME}")
    endif()
    foreach(dep ${BOLTDB_CC_LIB_DEPS})
      if(${dep} MATCHES "^boltdb::(.*)")
        # for DLL builds many libs are not created, but add
        # the pkgconfigs nevertheless, pointing to the dll.
        if(_build_type STREQUAL "dll")
          # hide this MATCHES in an if-clause so it doesn't overwrite
          # the CMAKE_MATCH_1 from (${dep} MATCHES "^boltdb::(.*)")
          if(NOT PC_DEPS MATCHES "boltdb_dll")
            # Join deps with commas.
            if(PC_DEPS)
              set(PC_DEPS "${PC_DEPS},")
            endif()
            # don't duplicate dll-dep if it exists already
            set(PC_DEPS "${PC_DEPS} boltdb_dll = ${PC_VERSION}")
            set(LNK_LIB "${LNK_LIB} -lboltdb_dll")
          endif()
        else()
          # Join deps with commas.
          if(PC_DEPS)
            set(PC_DEPS "${PC_DEPS},")
          endif()
          set(PC_DEPS "${PC_DEPS} boltdb_${CMAKE_MATCH_1} = ${PC_VERSION}")
        endif()
      endif()
    endforeach()
    foreach(cflag ${BOLTDB_CC_LIB_COPTS})
      # Strip out the CMake-specific `SHELL:` prefix, which is used to construct
      # a group of space-separated options.
      # https://cmake.org/cmake/help/v3.30/command/target_compile_options.html#option-de-duplication
      string(REGEX REPLACE "^SHELL:" "" cflag "${cflag}")
      if(${cflag} MATCHES "^-Xarch_")
        # An -Xarch_ flag implies that its successor only applies to the
        # specified platform. Such option groups are each specified in a single
        # `SHELL:`-prefixed string in the COPTS list, which we simply ignore.
      elseif(${cflag} MATCHES "^(-Wno-|/wd)")
        # These flags are needed to suppress warnings that might fire in our headers.
        set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
      elseif(${cflag} MATCHES "^(-W|/w[1234eo])")
        # Don't impose our warnings on others.
      elseif(${cflag} MATCHES "^-m")
        # Don't impose CPU instruction requirements on others, as
        # the code performs feature detection on runtime.
      else()
        set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
      endif()
    endforeach()
    string(REPLACE ";" " " PC_LINKOPTS "${BOLTDB_CC_LIB_LINKOPTS}")
    FILE(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/lib/pkgconfig/boltdb_${_NAME}.pc" CONTENT "prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=${prefix}
libdir=${CMAKE_INSTALL_FULL_LIBDIR}
includedir=${CMAKE_INSTALL_FULL_INCLUDEDIR}

Name: boltdb_${_NAME}
Description: boltdb ${_NAME} library
URL: https://boltdb.io/
Version: ${PC_VERSION}
Requires:${PC_DEPS}
Libs: -L${libdir} $<$<NOT:$<BOOL:${BOLTDB_CC_LIB_IS_INTERFACE}>>:${LNK_LIB}> ${PC_LINKOPTS}
Cflags: -I${includedir}${PC_CFLAGS}
")
    INSTALL(FILES "${CMAKE_BINARY_DIR}/lib/pkgconfig/boltdb_${_NAME}.pc"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  endif()

  if(NOT BOLTDB_CC_LIB_IS_INTERFACE)
    if(_build_type STREQUAL "dll_dep")
      # This target depends on the DLL. When adding dependencies to this target,
      # any depended-on-target which is contained inside the DLL is replaced
      # with a dependency on the DLL.
      add_library(${_NAME} STATIC "")
      target_sources(${_NAME} PRIVATE ${BOLTDB_CC_LIB_SRCS} ${BOLTDB_CC_LIB_HDRS})
      boltdb_internal_dll_targets(
        DEPS ${BOLTDB_CC_LIB_DEPS}
        OUTPUT _dll_deps
      )
      target_link_libraries(${_NAME}
        PUBLIC ${_dll_deps}
        PRIVATE
          ${BOLTDB_CC_LIB_LINKOPTS}
          ${BOLTDB_DEFAULT_LINKOPTS}
      )

      if (BOLTDB_CC_LIB_TESTONLY)
        set(_gtest_link_define "GTEST_LINKED_AS_SHARED_LIBRARY=1")
      else()
        set(_gtest_link_define)
      endif()

      target_compile_definitions(${_NAME}
        PUBLIC
          BOLTDB_CONSUME_DLL
          "${_gtest_link_define}"
      )

    elseif(_build_type STREQUAL "static" OR _build_type STREQUAL "shared")
      add_library(${_NAME} "")
      target_sources(${_NAME} PRIVATE ${BOLTDB_CC_LIB_SRCS} ${BOLTDB_CC_LIB_HDRS})
      if(APPLE)
        set_target_properties(${_NAME} PROPERTIES
          INSTALL_RPATH "@loader_path")
      elseif(UNIX)
        set_target_properties(${_NAME} PROPERTIES
          INSTALL_RPATH "$ORIGIN")
      endif()
      target_link_libraries(${_NAME}
      PUBLIC ${BOLTDB_CC_LIB_DEPS}
      PRIVATE
        ${BOLTDB_CC_LIB_LINKOPTS}
        ${BOLTDB_DEFAULT_LINKOPTS}
      )
    else()
      message(FATAL_ERROR "Invalid build type: ${_build_type}")
    endif()

    # Linker language can be inferred from sources, but in the case of DLLs we
    # don't have any .cc files so it would be ambiguous. We could set it
    # explicitly only in the case of DLLs but, because "CXX" is always the
    # correct linker language for static or for shared libraries, we set it
    # unconditionally.
    set_property(TARGET ${_NAME} PROPERTY LINKER_LANGUAGE "CXX")

    target_include_directories(${_NAME} ${BOLTDB_INTERNAL_INCLUDE_WARNING_GUARD}
      PUBLIC
        "$<BUILD_INTERFACE:${BOLTDB_COMMON_INCLUDE_DIRS}>"
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_options(${_NAME}
      PRIVATE ${BOLTDB_CC_LIB_COPTS})
    target_compile_definitions(${_NAME} PUBLIC ${BOLTDB_CC_LIB_DEFINES})

    # Add all boltdb targets to a a folder in the IDE for organization.
    if(BOLTDB_CC_LIB_PUBLIC)
      set_property(TARGET ${_NAME} PROPERTY FOLDER ${BOLTDB_IDE_FOLDER})
    elseif(BOLTDB_CC_LIB_TESTONLY)
      set_property(TARGET ${_NAME} PROPERTY FOLDER ${BOLTDB_IDE_FOLDER}/test)
    else()
      set_property(TARGET ${_NAME} PROPERTY FOLDER ${BOLTDB_IDE_FOLDER}/internal)
    endif()

    if(BOLTDB_PROPAGATE_CXX_STD)
      # boltdb libraries require C++17 as the current minimum standard. When
      # compiled with a higher standard (either because it is the compiler's
      # default or explicitly requested), then boltdb requires that standard.
      target_compile_features(${_NAME} PUBLIC ${BOLTDB_INTERNAL_CXX_STD_FEATURE})
    endif()

    # When being installed, we lose the boltdb_ prefix.  We want to put it back
    # to have properly named lib files.  This is a no-op when we are not being
    # installed.
    if(BOLTDB_ENABLE_INSTALL)
      set_target_properties(${_NAME} PROPERTIES
        OUTPUT_NAME "boltdb_${_NAME}"
        SOVERSION "${BOLTDB_SOVERSION}"
      )
    endif()
  else()
    # Generating header-only library
    add_library(${_NAME} INTERFACE)
    target_include_directories(${_NAME} ${BOLTDB_INTERNAL_INCLUDE_WARNING_GUARD}
      INTERFACE
        "$<BUILD_INTERFACE:${BOLTDB_COMMON_INCLUDE_DIRS}>"
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      )

    if (_build_type STREQUAL "dll")
        set(BOLTDB_CC_LIB_DEPS boltdb_dll)
    endif()

    target_link_libraries(${_NAME}
      INTERFACE
        ${BOLTDB_CC_LIB_DEPS}
        ${BOLTDB_CC_LIB_LINKOPTS}
        ${BOLTDB_DEFAULT_LINKOPTS}
    )
    target_compile_definitions(${_NAME} INTERFACE ${BOLTDB_CC_LIB_DEFINES})

    if(BOLTDB_PROPAGATE_CXX_STD)
      # boltdb libraries require C++17 as the current minimum standard.
      # Top-level application CMake projects should ensure a consistent C++
      # standard for all compiled sources by setting CMAKE_CXX_STANDARD.
      target_compile_features(${_NAME} INTERFACE ${BOLTDB_INTERNAL_CXX_STD_FEATURE})
    endif()
  endif()

  if(BOLTDB_ENABLE_INSTALL)
    install(TARGETS ${_NAME} EXPORT ${PROJECT_NAME}Targets
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()

  message(VERBOSE "Created boltdb_cc_library target: ${_NAME}")
  add_library(boltdb::${BOLTDB_CC_LIB_NAME} ALIAS ${_NAME})
endfunction()

# boltdb_cc_test()
#
# CMake function to imitate Bazel's cc_test rule.
#
# Parameters:
# NAME: name of target (see Usage below)
# SRCS: List of source files for the binary
# DEPS: List of other libraries to be linked in to the binary targets
# COPTS: List of private compile options
# DEFINES: List of public defines
# LINKOPTS: List of link options
#
# Note:
# By default, boltdb_cc_test will always create a binary named boltdb_${NAME}.
# This will also add it to ctest list as boltdb_${NAME}.
#
# Usage:
# boltdb_cc_library(
#   NAME
#     awesome
#   HDRS
#     "a.h"
#   SRCS
#     "a.cc"
#   PUBLIC
# )
#
# boltdb_cc_test(
#   NAME
#     awesome_test
#   SRCS
#     "awesome_test.cc"
#   DEPS
#     boltdb::awesome
#     GTest::gmock
#     GTest::gtest_main
# )
function(boltdb_cc_test)
  cmake_parse_arguments(BOLTDB_CC_TEST
    ""
    "NAME"
    "SRCS;COPTS;DEFINES;LINKOPTS;DEPS"
    ${ARGN}
  )

  set(_NAME "boltdb_${BOLTDB_CC_TEST_NAME}")

  add_executable(${_NAME} "")
  target_sources(${_NAME} PRIVATE ${BOLTDB_CC_TEST_SRCS})
  target_include_directories(${_NAME}
    PUBLIC ${BOLTDB_COMMON_INCLUDE_DIRS}
  )

  if (${BOLTDB_BUILD_DLL})
    target_compile_definitions(${_NAME}
      PUBLIC
        ${BOLTDB_CC_TEST_DEFINES}
        BOLTDB_CONSUME_DLL
        BOLTDB_CONSUME_TEST_DLL
        GTEST_LINKED_AS_SHARED_LIBRARY=1
    )

    # Replace dependencies on targets inside the DLL with boltdb_dll itself.
    boltdb_internal_dll_targets(
      DEPS ${BOLTDB_CC_TEST_DEPS}
      OUTPUT BOLTDB_CC_TEST_DEPS
    )
    boltdb_internal_dll_targets(
      DEPS ${BOLTDB_CC_TEST_LINKOPTS}
      OUTPUT BOLTDB_CC_TEST_LINKOPTS
    )
  else()
    target_compile_definitions(${_NAME}
      PUBLIC
        ${BOLTDB_CC_TEST_DEFINES}
    )
  endif()
  target_compile_options(${_NAME}
    PRIVATE ${BOLTDB_CC_TEST_COPTS}
  )

  target_link_libraries(${_NAME}
    PUBLIC ${BOLTDB_CC_TEST_DEPS}
    PRIVATE ${BOLTDB_CC_TEST_LINKOPTS}
  )
  # Add all boltdb targets to a folder in the IDE for organization.
  set_property(TARGET ${_NAME} PROPERTY FOLDER ${BOLTDB_IDE_FOLDER}/test)

  if(BOLTDB_PROPAGATE_CXX_STD)
    # boltdb libraries require C++17 as the current minimum standard.
    # Top-level application CMake projects should ensure a consistent C++
    # standard for all compiled sources by setting CMAKE_CXX_STANDARD.
    target_compile_features(${_NAME} PUBLIC ${BOLTDB_INTERNAL_CXX_STD_FEATURE})
  endif()

  add_test(NAME ${_NAME} COMMAND ${_NAME})
endfunction()
