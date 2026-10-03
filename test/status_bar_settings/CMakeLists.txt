add_executable(StatusBarSettingsTest
  StatusBarSettingsTest.cpp
)

target_include_directories(StatusBarSettingsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/test/shims
)

target_link_libraries(StatusBarSettingsTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(StatusBarSettingsTest)
