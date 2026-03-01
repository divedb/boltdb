cmake_minimum_required(VERSION 3.20)

project(
    boltdb
    VERSION 0.1.0
    LANGUAGES CXX
)

add_subdirectory(third_party/mio)

option(BOLTDB_BUILD_TESTS "Build tests" ON)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(boltdb INTERFACE)
add_library(boltdb::boltdb ALIAS boltdb)

target_compile_features(boltdb INTERFACE cxx_std_20)
target_include_directories(
    boltdb
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/boltdb
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(BOLTDB_BUILD_TESTS)
    include(FindGTest)
    enable_testing()
    add_subdirectory(tests)
endif()
