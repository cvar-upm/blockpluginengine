cmake_minimum_required(VERSION 3.8)
project(bpe_ros_boundary_plugins)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(block_plugin_engine REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(bpe_ros_boundary_plugins SHARED
  src/ros_input_block.cpp
  src/ros_output_block.cpp
)

target_compile_features(bpe_ros_boundary_plugins PUBLIC cxx_std_20)

target_include_directories(bpe_ros_boundary_plugins
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

ament_target_dependencies(bpe_ros_boundary_plugins
  block_plugin_engine
  pluginlib
  rclcpp
)

# One pluginlib manifest per package: include the test transform only in test builds.
if(BUILD_TESTING)
  set(BPE_BOUNDARY_PLUGIN_XML test/all_plugins.xml)
else()
  set(BPE_BOUNDARY_PLUGIN_XML plugins.xml)
endif()
pluginlib_export_plugin_description_file(block_plugin_engine ${BPE_BOUNDARY_PLUGIN_XML})

install(
  TARGETS bpe_ros_boundary_plugins
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)


if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(std_msgs REQUIRED)
  find_package(geometry_msgs REQUIRED)

  add_library(bpe_boundary_test_plugins SHARED
    test/plugins/test_01_multi_transform_block.cpp
  )
  target_compile_features(bpe_boundary_test_plugins PUBLIC cxx_std_20)
  ament_target_dependencies(bpe_boundary_test_plugins
    block_plugin_engine pluginlib rclcpp std_msgs geometry_msgs)
  install(TARGETS bpe_boundary_test_plugins LIBRARY DESTINATION lib)

  ament_add_gtest(test_01_ros_boundary_round_trip
    test/test_01_ros_boundary_round_trip.cpp)
  ament_target_dependencies(test_01_ros_boundary_round_trip
    block_plugin_engine pluginlib rclcpp std_msgs geometry_msgs)
  target_link_libraries(test_01_ros_boundary_round_trip yaml-cpp)
  target_compile_definitions(test_01_ros_boundary_round_trip PRIVATE
    BPE_BOUNDARY_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/config")

  # pluginlib needs the package manifests and DSOs installed by colcon build.
  # Keep the existing overlay paths intact when run directly through CTest.
  get_filename_component(BPE_ENGINE_TEST_PREFIX
    "${block_plugin_engine_DIR}/../../.." ABSOLUTE)
  set_tests_properties(test_01_ros_boundary_round_trip PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}:${BPE_ENGINE_TEST_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:${BPE_ENGINE_TEST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")
endif()

ament_package()
