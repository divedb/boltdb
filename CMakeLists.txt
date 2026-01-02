cmake_minimum_required(VERSION 3.20)
project(boltdb LANGUAGES CXX)

set(BOLTDB_SOVERSION 0)
# Output directory is correct by default for most build setups. However, when
# building boltdb as a DLL, it is important to have the DLL in the same
# directory as the executable using it. Thus, we put all executables in a single
# /bin directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

if(BOLTDB_MSVC_STATIC_RUNTIME)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set(BOLTDB_USING_CLANG ON)
else()
  set(BOLTDB_USING_CLANG OFF)
endif()


# Allow the user to specify the CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif (POLICY CMP0141)

# =============================================================================
# General options.
# =============================================================================

option(BOLTDB_PROPAGATE_CXX_STD 
  "Use CMake C++ standard meta features (e.g. cxx_std_20) that propagate to targets that link to boltdb"
  ON)

option(BOLTDB_USE_SYSTEM_INCLUDES
  "Silence warnings in boltdb headers by marking them as SYSTEM includes"
  OFF)

option(BOLTDB_MSVC_STATIC_RUNTIME
  "Link static runtime libraries"
  OFF)

option(BOLTDB_BUILD_MONOLITHIC_SHARED_LIBS
  "Build boltdb as a single shared library (always enabled for Windows)"
  OFF
)

option(BOLTDB_BUILD_TESTING "If ON, boltdb will build all of boltdb's own tests." OFF)

if(NOT BUILD_SHARED_LIBS AND BOLTDB_BUILD_MONOLITHIC_SHARED_LIBS)
  message(WARNING "Not building a shared library because BUILD_SHARED_LIBS is not set. Ignoring BOLTDB_BUILD_MONOLITHIC_SHARED_LIBS.")
endif()

option(BOLTDB_ENABLE_INSTALL "Enable install rule" OFF)


list(APPEND CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_LIST_DIR}/cmake
  ${CMAKE_CURRENT_LIST_DIR}/boltdb/copts
)
list(APPEND BOLTDB_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(boltdbDll)
include(boltdbHelpers)
include(CMakeDependentOption)

# =============================================================================
# Third-party
# =============================================================================

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PUSH OFF CACHE BOOL "" FORCE)
set(ENABLE_PULL OFF CACHE BOOL "" FORCE)

# add_subdirectory(src)
add_subdirectory(third_party/prometheus-cpp)

if(BOLTDB_BUILD_TESTING)
  include(CTest)
  set(ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)

  add_subdirectory(third_party/googletest)
  add_subdirectory(third_party/benchmark)
  add_subdirectory(tests)
endif()

if(BOLTDB_ENABLE_INSTALL)
  # boltdb:lts-remove-begin(system installation is supported for LTS releases)
  # We don't support system-wide installation
  list(APPEND SYSTEM_INSTALL_DIRS "/usr/local" "/usr" "/opt/" "/opt/local" "c:/Program Files/${PROJECT_NAME}")
  if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX IN_LIST SYSTEM_INSTALL_DIRS)
    message(WARNING "  The default and system-level install directories are unsupported except in LTS   releases of boltdb.  Please set CMAKE_INSTALL_PREFIX to install boltdb in your   source or build tree directly.    ")
  endif()
  # boltdb:lts-remove-end

  # install as a subdirectory only
  install(EXPORT ${PROJECT_NAME}Targets
    NAMESPACE boltdb::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  )

  configure_package_config_file(
    cmake/boltdbConfig.cmake.in
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  )
  install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  )

  # boltdb only has a version in LTS releases.  This mechanism is accomplished
  # boltdb's internal Copybara (https://github.com/google/copybara) workflows and
  # isn't visible in the CMake buildsystem itself.
  if(boltdb_VERSION)
    write_basic_package_version_file(
      "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
      COMPATIBILITY ExactVersion
    )

    install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )
  endif()  # boltdb_VERSION

  # Install the headers except for "options.h" which is installed separately.
  install(DIRECTORY boltdb
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
      PATTERN "*.inc"
      PATTERN "*.h"
      PATTERN "options.h" EXCLUDE
      PATTERN "copts" EXCLUDE
      PATTERN "testdata" EXCLUDE
    )

  # Handle features that require at least C++20.
  if (BOLTDB_INTERNAL_AT_LEAST_CXX20)
    foreach(FEATURE "ORDERING")
      string(REPLACE
      "#define BOLTDB_OPTION_USE_STD_${FEATURE} 2"
      "#define BOLTDB_OPTION_USE_STD_${FEATURE} 1"
      BOLTDB_INTERNAL_OPTIONS_H_PINNED
      "${BOLTDB_INTERNAL_OPTIONS_H_CONTENTS}")
      set(BOLTDB_INTERNAL_OPTIONS_H_CONTENTS "${BOLTDB_INTERNAL_OPTIONS_H_PINNED}")
    endforeach()
  endif()

  # Handle features that require at least C++17.
  if (BOLTDB_INTERNAL_AT_LEAST_CXX17)
    foreach(FEATURE "ANY" "OPTIONAL" "STRING_VIEW" "VARIANT")
      string(REPLACE
      "#define BOLTDB_OPTION_USE_STD_${FEATURE} 2"
      "#define BOLTDB_OPTION_USE_STD_${FEATURE} 1"
      BOLTDB_INTERNAL_OPTIONS_H_PINNED
      "${BOLTDB_INTERNAL_OPTIONS_H_CONTENTS}")
      set(BOLTDB_INTERNAL_OPTIONS_H_CONTENTS "${BOLTDB_INTERNAL_OPTIONS_H_PINNED}")
    endforeach()
  endif()

  # Any feature that still has the value of 2 (because it was not handled above)
  # should be set to 0.
  string(REGEX REPLACE
    "#define BOLTDB_OPTION_USE_STD_([^ ]*) 2"
    "#define BOLTDB_OPTION_USE_STD_\\1 0"
    BOLTDB_INTERNAL_OPTIONS_H_PINNED
    "${BOLTDB_INTERNAL_OPTIONS_H_CONTENTS}")

  # If the file already exists, check if it matches the new contents.
  # This avoids writing the file if it is already up-to-date when the CMake
  # generation is triggered and triggering unnecessary rebuilds.
  set(BOLTDB_INTERNAL_OPTIONS_H_PINNED_NEEDS_UPDATE TRUE)
  if (EXISTS "${CMAKE_BINARY_DIR}/options-pinned.h")
    file(READ "${CMAKE_BINARY_DIR}/options-pinned.h" BOLTDB_INTERNAL_OPTIONS_PINNED_H_CONTENTS)
    if ("${BOLTDB_INTERNAL_OPTIONS_H_PINNED}" STREQUAL "${BOLTDB_INTERNAL_OPTIONS_PINNED_H_CONTENTS}")
      set(BOLTDB_INTERNAL_OPTIONS_H_PINNED_NEEDS_UPDATE FALSE)
    endif()
  endif()

  # If the file needs an update, generate it.
  if (BOLTDB_INTERNAL_OPTIONS_H_PINNED_NEEDS_UPDATE)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/options-pinned.h" CONTENT "${BOLTDB_INTERNAL_OPTIONS_H_PINNED}")
  endif()

  install(FILES "${CMAKE_BINARY_DIR}/options-pinned.h"
         DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/boltdb/base
         RENAME "options.h")

endif()  # BOLTDB_ENABLE_INSTALL
  