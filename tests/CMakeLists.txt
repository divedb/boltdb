include(GoogleTest)

add_executable(page_test page_test.cc)
# add_executable(bucket_test bucket_test.cc)
# add_executable(error_test error_test.cc)

target_link_libraries(
    page_test
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
# gtest_discover_tests(bucket_test)
# gtest_discover_tests(error_test)
