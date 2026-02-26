if(TARGET GTest::gtest)
    set(GTest_FOUND TRUE)
    return()
endif()

# Prefer a system-installed GoogleTest package first.
find_package(GTest CONFIG QUIET NO_MODULE)
if(GTest_FOUND)
    return()
endif()

option(
    BOLTDB_GTEST_AUTO_SUBMODULE
    "Automatically add/update third_party/googletest when system GTest is unavailable"
    ON
)

set(_boltdb_gtest_source_dir "${CMAKE_SOURCE_DIR}/third_party/googletest")
set(_boltdb_gtest_binary_dir "${CMAKE_BINARY_DIR}/third_party/googletest")

if(NOT EXISTS "${_boltdb_gtest_source_dir}/CMakeLists.txt")
    if(NOT BOLTDB_GTEST_AUTO_SUBMODULE)
        message(
            FATAL_ERROR
            "GTest not found on the system and vendored googletest is missing at "
            "${_boltdb_gtest_source_dir}. Install GTest or enable "
            "BOLTDB_GTEST_AUTO_SUBMODULE."
        )
    endif()

    find_package(Git QUIET)
    if(NOT Git_FOUND)
        message(
            FATAL_ERROR
            "GTest not found and Git is unavailable, cannot add googletest submodule."
        )
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    message(STATUS "GTest not found; adding googletest submodule to third_party/googletest")
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" submodule add
            https://github.com/google/googletest.git
            third_party/googletest
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _boltdb_gtest_add_result
        OUTPUT_VARIABLE _boltdb_gtest_add_stdout
        ERROR_VARIABLE _boltdb_gtest_add_stderr
    )

    if(NOT _boltdb_gtest_add_result EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to add googletest submodule.\n"
            "stdout:\n${_boltdb_gtest_add_stdout}\n"
            "stderr:\n${_boltdb_gtest_add_stderr}\n"
            "You can also install a system GTest package and reconfigure."
        )
    endif()
endif()

add_subdirectory(
    "${_boltdb_gtest_source_dir}"
    "${_boltdb_gtest_binary_dir}"
    EXCLUDE_FROM_ALL
)

# Compatibility aliases for older/newer target naming.
if(TARGET gtest AND NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
endif()
if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
endif()
if(TARGET gmock AND NOT TARGET GTest::gmock)
    add_library(GTest::gmock ALIAS gmock)
endif()
if(TARGET gmock_main AND NOT TARGET GTest::gmock_main)
    add_library(GTest::gmock_main ALIAS gmock_main)
endif()

set(GTest_FOUND TRUE)
