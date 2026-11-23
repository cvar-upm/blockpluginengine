# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)
project(bpe_test)

find_package(ament_cmake REQUIRED)
find_package(block_plugin_engine REQUIRED)
find_package(pluginlib REQUIRED)
find_package(geometry_msgs REQUIRED)

# Integration-test blocks live in this package, self-contained and dynamically loaded.
add_library(bpe_test_plugins SHARED
  src/twist_producer_block.cpp
  src/twist_transform_block.cpp
  src/twist_checker_block.cpp
  src/multi_twist_transform_block.cpp
)
target_compile_features(bpe_test_plugins PUBLIC cxx_std_20)
target_include_directories(bpe_test_plugins PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
ament_target_dependencies(bpe_test_plugins block_plugin_engine pluginlib geometry_msgs)
pluginlib_export_plugin_description_file(block_plugin_engine plugins.xml)
install(TARGETS bpe_test_plugins LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)


if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(geometry_msgs REQUIRED)
  find_package(yaml-cpp REQUIRED)

  ament_add_gtest(test_01_twist_pipeline
    test/test_01_twist_pipeline.cpp
  )
  target_compile_features(test_01_twist_pipeline PRIVATE cxx_std_20)
  ament_target_dependencies(test_01_twist_pipeline
    block_plugin_engine pluginlib
  )
  target_include_directories(test_01_twist_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_dependencies(test_01_twist_pipeline bpe_test_plugins)
  target_link_libraries(test_01_twist_pipeline yaml-cpp)

  ament_add_gtest(test_02_multi_twist_pipeline
    test/test_02_multi_twist_pipeline.cpp
  )
  target_compile_features(test_02_multi_twist_pipeline PRIVATE cxx_std_20)
  target_compile_definitions(test_02_multi_twist_pipeline PRIVATE
    BPE_TEST_MULTI_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/test_02_multi_twist_pipeline.yaml"
  )
  ament_target_dependencies(test_02_multi_twist_pipeline
    block_plugin_engine pluginlib geometry_msgs
  )
  target_include_directories(test_02_multi_twist_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_dependencies(test_02_multi_twist_pipeline bpe_test_plugins)
  target_link_libraries(test_02_multi_twist_pipeline yaml-cpp)
  # Deterministic cross-thread races on the real typed BPE blackboard.
  find_package(Threads REQUIRED)
  ament_add_gtest(test_04_blackboard_concurrency
    test/test_04_blackboard_concurrency.cpp
  )
  target_compile_features(test_04_blackboard_concurrency PRIVATE cxx_std_20)
  ament_target_dependencies(test_04_blackboard_concurrency block_plugin_engine)
  target_link_libraries(test_04_blackboard_concurrency Threads::Threads)

  # Four real scheduler workers: TIMER writer, EVENT reader, two TIMER readers.
  ament_add_gtest(test_05_multischeduler_blackboard_concurrency
    test/test_05_multischeduler_blackboard_concurrency.cpp
    TIMEOUT 75
  )
  target_compile_features(test_05_multischeduler_blackboard_concurrency PRIVATE cxx_std_20)
  ament_target_dependencies(test_05_multischeduler_blackboard_concurrency
    block_plugin_engine pluginlib
  )
  target_link_libraries(test_05_multischeduler_blackboard_concurrency
    yaml-cpp Threads::Threads
  )
  get_filename_component(BPE_TEST_SCHEDULER_PREFIX
    "${block_plugin_engine_DIR}/../../.." ABSOLUTE)
  set_tests_properties(test_05_multischeduler_blackboard_concurrency PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${BPE_TEST_SCHEDULER_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${BPE_TEST_SCHEDULER_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")

  # Same four-scheduler concurrency scenario at shorter, parametrizable
  # periods. Reuse the source, not a copied test implementation.
  ament_add_gtest(test_06_multischeduler_blackboard_high_frequency
    test/test_05_multischeduler_blackboard_concurrency.cpp
    TIMEOUT 75
  )
  target_compile_features(test_06_multischeduler_blackboard_high_frequency PRIVATE cxx_std_20)
  target_compile_definitions(test_06_multischeduler_blackboard_high_frequency PRIVATE
    BPE_CONCURRENCY_STRESS_TEST=1)
  ament_target_dependencies(test_06_multischeduler_blackboard_high_frequency
    block_plugin_engine pluginlib
  )
  target_link_libraries(test_06_multischeduler_blackboard_high_frequency
    yaml-cpp Threads::Threads
  )
  set_tests_properties(test_06_multischeduler_blackboard_high_frequency PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${BPE_TEST_SCHEDULER_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${BPE_TEST_SCHEDULER_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")

  # Exercise the five end-to-end integration scenarios against the real BPE
  # executable, without reconstructing its lifecycle/plugin loaders.
  find_package(ament_cmake_pytest REQUIRED)
  find_package(bpe_ros_boundary_plugins REQUIRED)

  ament_add_pytest_test(test_03_engine_integration
    test/test_03_engine_integration.py
    TIMEOUT 120
  )

  get_filename_component(BPE_TEST_ENGINE_PREFIX
    "${block_plugin_engine_DIR}/../../.." ABSOLUTE)
  get_filename_component(BPE_TEST_BOUNDARY_PREFIX
    "${bpe_ros_boundary_plugins_DIR}/../../.." ABSOLUTE)

  set_tests_properties(test_03_engine_integration PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}:${BPE_TEST_ENGINE_PREFIX}:${BPE_TEST_BOUNDARY_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:${BPE_TEST_ENGINE_PREFIX}/lib:${BPE_TEST_BOUNDARY_PREFIX}/lib:$ENV{LD_LIBRARY_PATH};BPE_TEST_CONFIG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/config;BPE_TEST_EXECUTABLE=${BPE_TEST_ENGINE_PREFIX}/lib/block_plugin_engine/block_plugin_engine")
endif()

ament_package()
