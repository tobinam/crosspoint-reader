# Optional integration tests against the exact pinned InkCPP checkout.
if(NOT INKCPP_SOURCE_DIR)
  return()
endif()
add_compile_definitions(INKCPP_NO_STD INKCPP_NO_RTTI INKCPP_NO_EXCEPTIONS)
add_subdirectory(${INKCPP_SOURCE_DIR}/shared ${CMAKE_CURRENT_BINARY_DIR}/ink-shared EXCLUDE_FROM_ALL)
add_subdirectory(${INKCPP_SOURCE_DIR}/inkcpp ${CMAKE_CURRENT_BINARY_DIR}/ink-runtime EXCLUDE_FROM_ALL)
add_executable(InkStorySessionTest InkStorySessionTest.cpp StoryTextLayoutTest.cpp ../../src/stories/InkStorySession.cpp)
target_compile_definitions(InkStorySessionTest PRIVATE
  INK_STORY_HOST_TEST=1
  STORY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../stories/examples/low-tide.bin"
)
target_include_directories(InkStorySessionTest PRIVATE ../../src ../../freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(InkStorySessionTest PRIVATE inkcpp crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(InkStorySessionTest)
