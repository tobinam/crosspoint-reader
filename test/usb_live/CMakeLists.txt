add_executable(UsbLiveProtocolTest UsbLiveProtocolTest.cpp)
target_include_directories(UsbLiveProtocolTest PRIVATE
  ../../src
  ../../freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(UsbLiveProtocolTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UsbLiveProtocolTest)

add_executable(UsbFileTransferTest UsbFileTransferTest.cpp)
target_include_directories(UsbFileTransferTest PRIVATE ../../src)
target_link_libraries(UsbFileTransferTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UsbFileTransferTest)
