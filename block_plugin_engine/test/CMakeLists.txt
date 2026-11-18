
add_library(bpe_test_support SHARED
  tools/event_log.cpp
)

target_include_directories(bpe_test_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(bpe_test_plugins SHARED
  plugins/test_03_basic_mock_block.cpp
  plugins/test_03_timer_mock_block.cpp
  plugins/test_03_failing_mock_block.cpp
  plugins/test_03_event_service_mock_block.cpp
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
  block_plugin_engine_core
  bpe_test_support
  yaml-cpp
)

pluginlib_export_plugin_description_file(
  block_plugin_engine
  test_03_plugins.xml
)

ament_add_gtest(test_03_block_plugin_manager
  test_03_block_plugin_manager.cpp
  mocks/scheduler_mock.cpp
  ${PROJECT_SOURCE_DIR}/src/block_plugin_manager.cpp
)

target_include_directories(test_03_block_plugin_manager
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

ament_target_dependencies(test_03_block_plugin_manager
  rclcpp
  pluginlib
)

target_link_libraries(test_03_block_plugin_manager
  block_plugin_engine_core
  bpe_test_support
  yaml-cpp
)

target_compile_definitions(test_03_block_plugin_manager
  PRIVATE
    BPE_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
    BPE_TEST_PLUGIN_XML="${CMAKE_CURRENT_SOURCE_DIR}/test_03_plugins.xml"
)

install(
  TARGETS
    bpe_test_support
    bpe_test_plugins
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_add_gtest(test_bpe_fair_scheduler
  test_bpe_fair_scheduler.cpp
)

target_include_directories(test_bpe_fair_scheduler
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_bpe_fair_scheduler
  bpe_fair_scheduler
  yaml-cpp
)

# This is a program to execute a performance test.
# It measures several stadistics about scheduler timings.

add_executable(scheduler_timing
  scheduler_timing.cpp
)

target_include_directories(scheduler_timing
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(scheduler_timing
  bpe_fair_scheduler
  yaml-cpp
)

install(
  TARGETS scheduler_timing
  DESTINATION lib/${PROJECT_NAME}
)

ament_add_gtest(test_01_ros_typed_blackboard
  test_01_ros_typed_blackboard.cpp
)

ament_target_dependencies(test_01_ros_typed_blackboard
  geometry_msgs
  rclcpp
)

target_include_directories(test_01_ros_typed_blackboard
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_01_ros_typed_blackboard
  block_plugin_engine_core
)

ament_add_gtest(test_bpe_multi_scheduler test_bpe_multi_scheduler.cpp)
target_include_directories(test_bpe_multi_scheduler PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_bpe_multi_scheduler bpe_multi_scheduler yaml-cpp)

ament_add_gtest(test_02_event_service
  test_02_event_service.cpp
)

target_link_libraries(test_02_event_service
  block_plugin_engine_core
)
