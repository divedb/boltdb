include(CheckCXXSourceCompiles)

check_cxx_source_compiles(
  [==[
#ifdef _MSC_VER
#  if _MSVC_LANG < 201703L
#    error "The compiler defaults or is configured for C++ < 17"
#  endif
#elif __cplusplus < 201703L
#  error "The compiler defaults or is configured for C++ < 17"
#endif
int main() { return 0; }
]==]
  BOLTDB_INTERNAL_AT_LEAST_CXX17)

check_cxx_source_compiles(
  [==[
#ifdef _MSC_VER
#  if _MSVC_LANG < 202002L
#    error "The compiler defaults or is configured for C++ < 20"
#  endif
#elif __cplusplus < 202002L
#  error "The compiler defaults or is configured for C++ < 20"
#endif
int main() { return 0; }
]==]
  BOLTDB_INTERNAL_AT_LEAST_CXX20)

if(BOLTDB_INTERNAL_AT_LEAST_CXX20)
  set(BOLTDB_INTERNAL_CXX_STD_FEATURE cxx_std_20)
elseif(BOLTDB_INTERNAL_AT_LEAST_CXX17)
  set(BOLTDB_INTERNAL_CXX_STD_FEATURE cxx_std_17)
else()
  message(FATAL_ERROR "The compiler defaults to or is configured for C++ < 17. C++ >= 17 is required and boltdb and all libraries that use boltdb must use the same C++ language standard")
endif()

function(boltdb_internal_dll_contains)
  cmake_parse_arguments(BOLTDB_INTERNAL_DLL
    ""
    "OUTPUT;TARGET"
    ""
    ${ARGN}
  )

  STRING(REGEX REPLACE "^boltdb::" "" _target ${BOLTDB_INTERNAL_DLL_TARGET})

  if (_target IN_LIST BOLTDB_INTERNAL_DLL_TARGETS)
    set(${BOLTDB_INTERNAL_DLL_OUTPUT} 1 PARENT_SCOPE)
  else()
    set(${BOLTDB_INTERNAL_DLL_OUTPUT} 0 PARENT_SCOPE)
  endif()
endfunction()

function(boltdb_internal_test_dll_contains)
  cmake_parse_arguments(BOLTDB_INTERNAL_TEST_DLL
    ""
    "OUTPUT;TARGET"
    ""
    ${ARGN}
  )

  STRING(REGEX REPLACE "^boltdb::" "" _target ${BOLTDB_INTERNAL_TEST_DLL_TARGET})

  if (_target IN_LIST BOLTDB_INTERNAL_TEST_DLL_TARGETS)
    set(${BOLTDB_INTERNAL_TEST_DLL_OUTPUT} 1 PARENT_SCOPE)
  else()
    set(${BOLTDB_INTERNAL_TEST_DLL_OUTPUT} 0 PARENT_SCOPE)
  endif()
endfunction()

function(boltdb_internal_dll_targets)
  cmake_parse_arguments(BOLTDB_INTERNAL_DLL
  ""
  "OUTPUT"
  "DEPS"
  ${ARGN}
  )

  set(_deps "")
  foreach(dep IN LISTS BOLTDB_INTERNAL_DLL_DEPS)
    boltdb_internal_dll_contains(TARGET ${dep} OUTPUT _dll_contains)
    boltdb_internal_test_dll_contains(TARGET ${dep} OUTPUT _test_dll_contains)
    if (_dll_contains)
      list(APPEND _deps boltdb_dll)
    elseif (_test_dll_contains)
      list(APPEND _deps boltdb_test_dll)
    else()
      list(APPEND _deps ${dep})
    endif()
  endforeach()

  # Because we may have added the DLL multiple times
  list(REMOVE_DUPLICATES _deps)
  set(${BOLTDB_INTERNAL_DLL_OUTPUT} "${_deps}" PARENT_SCOPE)
endfunction()

function(boltdb_make_dll)
  cmake_parse_arguments(BOLTDB_INTERNAL_MAKE_DLL
  ""
  "TEST"
  ""
  ${ARGN}
  )

  if (BOLTDB_INTERNAL_MAKE_DLL_TEST)
    set(_dll "boltdb_test_dll")
    set(_dll_files ${BOLTDB_INTERNAL_TEST_DLL_FILES})
    set(_dll_libs "boltdb_dll" "GTest::gtest" "GTest::gmock")
    set(_dll_compile_definitions "GTEST_LINKED_AS_SHARED_LIBRARY=1")
    set(_dll_includes ${boltdb_gtest_src_dir}/googletest/include ${boltdb_gtest_src_dir}/googlemock/include)
    set(_dll_consume "BOLTDB_CONSUME_TEST_DLL")
    set(_dll_build "BOLTDB_BUILD_TEST_DLL")
  else()
    set(_dll "boltdb_dll")
    set(_dll_files ${BOLTDB_INTERNAL_DLL_FILES})
    set(_dll_libs
      Threads::Threads
      # TODO(#1495): Use $<LINK_LIBRARY:FRAMEWORK,CoreFoundation> once our
      # minimum CMake version >= 3.24
      $<$<PLATFORM_ID:Darwin>:-Wl,-framework,CoreFoundation>
    )
    set(_dll_compile_definitions "")
    set(_dll_includes "")
    set(_dll_consume "BOLTDB_CONSUME_DLL")
    set(_dll_build "BOLTDB_BUILD_DLL")
  endif()

  add_library(
    ${_dll}
    SHARED
      ${_dll_files}
  )
  target_link_libraries(
    ${_dll}
    PRIVATE
      ${_dll_libs}
      ${BOLTDB_DEFAULT_LINKOPTS}
      $<$<BOOL:${ANDROID}>:-llog>
      $<$<BOOL:${MINGW}>:-ladvapi32>
      $<$<BOOL:${MINGW}>:-ldbghelp>
      $<$<BOOL:${MINGW}>:-lbcrypt>
  )
  set_target_properties(${_dll} PROPERTIES
    LINKER_LANGUAGE "CXX"
    SOVERSION ${BOLTDB_SOVERSION}
  )
  target_include_directories(
    ${_dll}
    PUBLIC
      "$<BUILD_INTERFACE:${BOLTDB_COMMON_INCLUDE_DIRS}>"
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${_dll_includes}
  )

  target_compile_options(
    ${_dll}
    PRIVATE
      ${BOLTDB_DEFAULT_COPTS}
  )

  foreach(cflag ${BOLTDB_CC_LIB_COPTS})
    if(${cflag} MATCHES "^(-Wno|/wd)")
      # These flags are needed to suppress warnings that might fire in our headers.
      set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
    elseif(${cflag} MATCHES "^(-W|/w[1234eo])")
      # Don't impose our warnings on others.
    else()
      set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
    endif()
  endforeach()
  string(REPLACE ";" " " PC_LINKOPTS "${BOLTDB_CC_LIB_LINKOPTS}")

  FILE(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/lib/pkgconfig/${_dll}.pc" CONTENT "prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=${prefix}
libdir=${CMAKE_INSTALL_FULL_LIBDIR}
includedir=${CMAKE_INSTALL_FULL_INCLUDEDIR}

Name: ${_dll}
Description: boltdb DLL library
URL: https://boltdb.io/
Version: ${boltdb_VERSION}
Libs: -L${libdir} $<$<NOT:$<BOOL:${BOLTDB_CC_LIB_IS_INTERFACE}>>:-l${_dll}> ${PC_LINKOPTS}
Cflags: -I${includedir}${PC_CFLAGS}
")
  INSTALL(FILES "${CMAKE_BINARY_DIR}/lib/pkgconfig/${_dll}.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

  target_compile_definitions(
    ${_dll}
    PUBLIC
      ${_dll_compile_definitions}
    PRIVATE
      ${_dll_build}
      NOMINMAX
    INTERFACE
      ${BOLTDB_CC_LIB_DEFINES}
      ${_dll_consume}
  )

  if(BOLTDB_PROPAGATE_CXX_STD)
    # boltdb libraries require C++17 as the current minimum standard. When
    # compiled with a higher minimum (either because it is the compiler's
    # default or explicitly requested), then boltdb requires that standard.
    target_compile_features(${_dll} PUBLIC ${BOLTDB_INTERNAL_CXX_STD_FEATURE})
  endif()

  install(TARGETS ${_dll} EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  add_library(boltdb::${_dll} ALIAS ${_dll})
endfunction()
