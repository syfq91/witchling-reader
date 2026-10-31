add_executable(TurnedFrameTest
  TurnedFrameTest.cpp
)

target_include_directories(TurnedFrameTest PRIVATE
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(TurnedFrameTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TurnedFrameTest)
