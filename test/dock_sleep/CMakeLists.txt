add_executable(DockSleepPolicyTest DockSleepPolicyTest.cpp)
target_include_directories(DockSleepPolicyTest PRIVATE ../../src)
target_link_libraries(DockSleepPolicyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DockSleepPolicyTest)
