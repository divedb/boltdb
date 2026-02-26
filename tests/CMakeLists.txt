include(GoogleTest)

add_executable(page_test page_test.cc)

target_link_libraries(
    page_test
    PRIVATE
        boltdb::boltdb
        GTest::gtest_main
)

gtest_discover_tests(page_test)
