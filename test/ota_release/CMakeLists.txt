add_executable(OtaReleaseTest
  OtaReleaseTest.cpp
  ${REPO_ROOT}/src/network/OtaRelease.cpp
)

target_include_directories(OtaReleaseTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(OtaReleaseTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaReleaseTest)
