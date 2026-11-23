cmake_minimum_required(VERSION 3.11)
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
ament_target_dependencies(bpe_examples_plugins block_plugin_engine pluginlib geometry_msgs std_msgs)
pluginlib_export_plugin_description_file(block_plugin_engine plugins.xml)
install(TARGETS bpe_examples_plugins LIBRARY DESTINATION lib)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})
ament_package()
