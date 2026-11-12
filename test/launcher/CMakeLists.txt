add_executable(LauncherLayoutTest LauncherLayoutTest.cpp)
target_include_directories(LauncherLayoutTest PRIVATE
  ../../src
  ../../freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(LauncherLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LauncherLayoutTest)
