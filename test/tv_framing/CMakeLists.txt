add_executable(TvFramingTest TvFramingTest.cpp)
target_include_directories(TvFramingTest PRIVATE ../../src)
target_link_libraries(TvFramingTest PRIVATE GTest::gtest_main)
gtest_discover_tests(TvFramingTest)
