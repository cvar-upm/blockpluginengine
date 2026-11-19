
add_library(bpe_test_support SHARED
  tools/event_log.cpp
  tools/simulated_clock.cpp
)

target_include_directories(bpe_test_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(bpe_test_plugins SHARED
  plugins/test_03_basic_mock_block.cpp
  plugins/test_03_timer_mock_block.cpp
  plugins/test_03_failing_mock_block.cpp
  plugins/test_03_signal_user_mock_block.cpp
  plugins/test_06_signal_producer_block.cpp
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

# Test block plugins are loaded from explicit XML paths by BlockPluginManager.
# Do not register a second pluginlib ament-index manifest for schedulers.


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

ament_add_gtest(test_04_bpe_fair_scheduler
  test_04_bpe_fair_scheduler.cpp
)

target_include_directories(test_04_bpe_fair_scheduler
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_04_bpe_fair_scheduler
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


# Hybrid scheduler functional coverage. The worker runs with SCHED_OTHER;
# no hard real-time deadline/latency assertions are made here.
ament_add_gtest(test_05_bpe_hybrid_fair_scheduler
  test_05_bpe_hybrid_fair_scheduler.cpp
)
target_include_directories(test_05_bpe_hybrid_fair_scheduler PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(test_05_bpe_hybrid_fair_scheduler
  bpe_hybrid_fair_scheduler
  bpe_test_support
  yaml-cpp
)

# Pluginlib-registered specializations with one shared virtual clock.
# They exist only in BUILD_TESTING, never in the production plugin manifest.
add_library(bpe_test_simulated_schedulers SHARED
  plugins/test_06_simulated_schedulers.cpp
)
target_compile_features(bpe_test_simulated_schedulers PUBLIC cxx_std_20)
target_include_directories(bpe_test_simulated_schedulers PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}
)
ament_target_dependencies(bpe_test_simulated_schedulers pluginlib)
target_link_libraries(bpe_test_simulated_schedulers
  bpe_test_support block_plugin_engine_core yaml-cpp Threads::Threads
)

ament_add_gtest(test_06_scheduler_integration
  test_06_scheduler_integration.cpp
  ${PROJECT_SOURCE_DIR}/src/scheduler_plugin_host.cpp
  ${PROJECT_SOURCE_DIR}/src/block_plugin_manager.cpp
)
target_compile_features(test_06_scheduler_integration PUBLIC cxx_std_20)
target_include_directories(test_06_scheduler_integration PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}
)
ament_target_dependencies(test_06_scheduler_integration rclcpp pluginlib)
target_link_libraries(test_06_scheduler_integration
  block_plugin_engine_core bpe_test_support yaml-cpp Threads::Threads
)
target_compile_definitions(test_06_scheduler_integration PRIVATE
  BPE_TEST_PLUGIN_XML="${CMAKE_CURRENT_SOURCE_DIR}/test_03_plugins.xml"
  BPE_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
)

# The test is run from build/ without sourcing install/setup.bash.
# pluginlib needs the installed package index and shared libraries.
# Preserve the ROS and overlay paths already provided by the environment.
set_tests_properties(test_06_scheduler_integration PROPERTIES
  ENVIRONMENT
    "AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}"
)

add_dependencies(test_06_scheduler_integration
  bpe_test_simulated_schedulers bpe_test_plugins
)
install(TARGETS bpe_test_simulated_schedulers LIBRARY DESTINATION lib)
