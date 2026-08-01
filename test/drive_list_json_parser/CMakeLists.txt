add_executable(DriveListJsonParserTest
  DriveListJsonParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/DriveListJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(DriveListJsonParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(DriveListJsonParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DriveListJsonParserTest)
