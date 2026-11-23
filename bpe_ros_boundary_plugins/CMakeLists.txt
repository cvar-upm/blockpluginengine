# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.8)
project(bpe_ros_boundary_plugins)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(block_plugin_engine REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)

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
  find_package(ament_cmake_pytest REQUIRED)
  find_package(std_msgs REQUIRED)
  find_package(geometry_msgs REQUIRED)

  add_library(bpe_boundary_test_plugins SHARED
    test/plugins/test_01_multi_transform_block.cpp
  )
  target_compile_features(bpe_boundary_test_plugins PUBLIC cxx_std_20)
  ament_target_dependencies(bpe_boundary_test_plugins
    block_plugin_engine pluginlib rclcpp std_msgs geometry_msgs)
  install(TARGETS bpe_boundary_test_plugins LIBRARY DESTINATION lib)

  # Exercise the real installed BlockPluginEngine executable, rather than
  # reconstructing its plugin loading/lifecycle in a test fixture.
  ament_add_pytest_test(test_01_ros_boundary_round_trip
    test/test_01_ros_boundary_round_trip.py
    TIMEOUT 45
  )

  get_filename_component(BPE_ENGINE_TEST_PREFIX
    "${block_plugin_engine_DIR}/../../.." ABSOLUTE)
  set_tests_properties(test_01_ros_boundary_round_trip PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}:${BPE_ENGINE_TEST_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:${BPE_ENGINE_TEST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH};BPE_BOUNDARY_TEST_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/test/config/test_01_RosRoundTrip.yaml;BPE_BOUNDARY_TEST_EXECUTABLE=${BPE_ENGINE_TEST_PREFIX}/lib/block_plugin_engine/block_plugin_engine")

endif()

ament_package()
