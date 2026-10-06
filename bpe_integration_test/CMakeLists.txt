cmake_minimum_required(VERSION 3.8)
project(bpe_integration_test)

find_package(ament_cmake REQUIRED)

install(
  DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(launch_testing_ament_cmake REQUIRED)

  add_launch_test(
    test/test_twist_pipeline.py
    TIMEOUT 30
  )
endif()

ament_package()
