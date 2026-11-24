# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(bpe_examples)
find_package(ament_cmake REQUIRED)
find_package(block_plugin_engine REQUIRED)
find_package(bpe_ros_boundary_plugins REQUIRED)
find_package(pluginlib REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_library(bpe_examples_plugins SHARED
  src/basic_blocks.cpp
  src/multi_blocks.cpp
  src/ros_transform.cpp
  src/hybrid_blocks.cpp
)
target_compile_features(bpe_examples_plugins PRIVATE cxx_std_20)
target_include_directories(bpe_examples_plugins PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bpe_examples_plugins
  block_plugin_engine::block_plugin_engine_core
  pluginlib::pluginlib
  ${geometry_msgs_TARGETS}
  ${std_msgs_TARGETS}
)
pluginlib_export_plugin_description_file(block_plugin_engine plugins.xml)
install(TARGETS bpe_examples_plugins LIBRARY DESTINATION lib)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})
ament_package()
