cmake_minimum_required(VERSION 3.8)
project(block_plugin_engine)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(yaml-cpp REQUIRED)

#
# Default scheduler implementation
#
# IBlockScheduler is public API, but BPE_SimpleScheduler is an internal
# implementation.  It is built as a shared library so the scheduler can
# remain physically separated from the engine executable.
#

add_library(bpe_simple_scheduler SHARED
  src/bpe_simple_scheduler.cpp
)

target_include_directories(bpe_simple_scheduler
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

#
# BlockPluginEngine executable
#
# BPE_LifecycleNode and BlockPluginManager are internal implementation
# details and are compiled directly into the executable.
#

add_executable(block_plugin_engine_node
  src/main.cpp
  src/bpe_lifecycle_node.cpp
  src/block_plugin_manager.cpp
)

target_include_directories(block_plugin_engine_node
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

ament_target_dependencies(block_plugin_engine_node
  rclcpp
  rclcpp_lifecycle
)

target_link_libraries(block_plugin_engine_node
  bpe_simple_scheduler
)

#
# Install only the public API headers.
#

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS
    bpe_simple_scheduler
    block_plugin_engine_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

#
# Public dependencies for packages implementing Blocks.
#
# yaml-cpp is exported because BlockConfig is part of the public API and is
# currently implemented as an alias of YAML::Node.
#

ament_export_include_directories(include)

ament_export_dependencies(
  rclcpp
  rclcpp_lifecycle
  yaml-cpp
)

ament_package()
