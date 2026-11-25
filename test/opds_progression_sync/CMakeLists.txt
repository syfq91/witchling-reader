if(NOT TARGET ArduinoJson)
  if(EXISTS "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src/ArduinoJson.h")
    add_library(ArduinoJson INTERFACE)
    target_include_directories(ArduinoJson INTERFACE
      "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src"
    )
  else()
    include(FetchContent)
    FetchContent_Declare(
      ArduinoJson
      GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
      GIT_TAG v7.4.1
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(ArduinoJson)
  endif()
endif()

add_executable(OpdsProgressionSyncTest
  OpdsProgressionSyncTest.cpp
  ${REPO_ROOT}/src/network/OpdsProgressionSync.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/test/zip_entry_reader/LoggingStub.cpp
)

target_include_directories(OpdsProgressionSyncTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shims
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Logging
)

target_link_libraries(OpdsProgressionSyncTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(OpdsProgressionSyncTest)
