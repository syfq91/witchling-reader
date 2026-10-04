add_executable(HalStorageRemoveDirTest
  HalStorageRemoveDirTest.cpp
)

target_include_directories(HalStorageRemoveDirTest PRIVATE
  ${REPO_ROOT}/test/zip_entry_reader   # HalStorage.h with real file + directory I/O
  ${REPO_ROOT}/test/shims              # Print.h, HardwareSerial.h, WString.h, Arduino.h
)

target_link_libraries(HalStorageRemoveDirTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HalStorageRemoveDirTest)
