
add_library(bpe_test_support SHARED
  test_event_log.cpp
)

target_include_directories(bpe_test_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(bpe_test_plugins SHARED
  plugins/test_basic_block.cpp
  plugins/test_timer_block.cpp
  plugins/test_failing_block.cpp
)

target_include_directories(bpe_test_plugins
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

ament_target_dependencies(bpe_test_plugins
  pluginlib
)

target_link_libraries(bpe_test_plugins
  bpe_test_support
  yaml-cpp
)

pluginlib_export_plugin_description_file(
  block_plugin_engine
  test_plugins.xml
)

ament_add_gtest(test_block_plugin_manager
  test_block_plugin_manager.cpp
  ${PROJECT_SOURCE_DIR}/src/block_plugin_manager.cpp
)

target_include_directories(test_block_plugin_manager
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

ament_target_dependencies(test_block_plugin_manager
  rclcpp
  pluginlib
)

target_link_libraries(test_block_plugin_manager
  bpe_test_support
  yaml-cpp
)

target_compile_definitions(test_block_plugin_manager
  PRIVATE
    BPE_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
    BPE_TEST_PLUGIN_XML="${CMAKE_CURRENT_SOURCE_DIR}/test_plugins.xml"
)

install(
  TARGETS
    bpe_test_support
    bpe_test_plugins
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_add_gtest(test_bpe_simple_scheduler
  test_bpe_simple_scheduler.cpp
)

target_include_directories(test_bpe_simple_scheduler
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_bpe_simple_scheduler
  bpe_simple_scheduler
  yaml-cpp
)