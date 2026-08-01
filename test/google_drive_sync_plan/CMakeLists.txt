add_executable(GoogleDriveSyncPlanTest
  GoogleDriveSyncPlanTest.cpp
  ${REPO_ROOT}/src/network/GoogleDriveSyncPlan.cpp
)

target_include_directories(GoogleDriveSyncPlanTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
)

target_link_libraries(GoogleDriveSyncPlanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GoogleDriveSyncPlanTest)
