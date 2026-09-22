cmake_minimum_required(VERSION 3.8)
project(block_plugin_engine)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

#
# Default scheduler
#

add_library(bpe_simple_scheduler SHARED
  src/bpe_simple_scheduler.cpp
)

target_include_directories(bpe_simple_scheduler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

#
# BlockPluginEngine core
#

add_library(bpe_core SHARED
  src/block_plugin_manager.cpp
  src/bpe_lifecycle_node.cpp
)

target_include_directories(bpe_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(bpe_core
  rclcpp
  rclcpp_lifecycle
)

target_link_libraries(bpe_core
  bpe_simple_scheduler
)

#
# Executable
#

add_executable(block_plugin_engine_node
  src/main.cpp
)

ament_target_dependencies(block_plugin_engine_node
  rclcpp
  rclcpp_lifecycle
)

target_link_libraries(block_plugin_engine_node
  bpe_core
)

#
# Install
#

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS
    bpe_simple_scheduler
    bpe_core
    block_plugin_engine_node
  EXPORT export_block_plugin_engine
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_dependencies(
  rclcpp
  rclcpp_lifecycle
)

ament_package()
