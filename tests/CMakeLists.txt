include(GoogleTest)

add_executable(page_test page_test.cc)
add_executable(freelist_test freelist_test.cc ../lib/freelist.cc)
# add_executable(bucket_test bucket_test.cc)
# add_executable(error_test error_test.cc)

target_link_libraries(
    page_test
    PRIVATE
        boltdb::boltdb
        GTest::gtest_main
)

target_link_libraries(
    freelist_test
    PRIVATE
        boltdb::boltdb
        GTest::gtest_main
)

# target_link_libraries(
#     bucket_test
#     PRIVATE
#         boltdb::boltdb
#         GTest::gtest_main
# )

# target_link_libraries(
#     error_test
#     PRIVATE
#         boltdb::boltdb
#         GTest::gtest_main
# )

gtest_discover_tests(page_test)
gtest_discover_tests(freelist_test)
# gtest_discover_tests(bucket_test)
# gtest_discover_tests(error_test)

if(BOLTDB_BUILD_BENCHMARKS)
    add_executable(freelist_benchmark freelist_benchmark.cc ../lib/freelist.cc)
    target_link_libraries(
        freelist_benchmark
        PRIVATE
            boltdb::boltdb
            benchmark::benchmark
    )
endif()
