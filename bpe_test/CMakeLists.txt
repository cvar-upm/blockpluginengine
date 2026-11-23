cmake_minimum_required(VERSION 3.11)
project(bpe_test)

find_package(ament_cmake REQUIRED)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(block_plugin_engine REQUIRED)
  find_package(bpe_system_test REQUIRED)
  find_package(pluginlib REQUIRED)
  find_package(geometry_msgs REQUIRED)
  find_package(yaml-cpp REQUIRED)

  ament_add_gtest(test_01_twist_pipeline
    test/test_01_twist_pipeline.cpp
  )
  target_compile_features(test_01_twist_pipeline PRIVATE cxx_std_20)
  ament_target_dependencies(test_01_twist_pipeline
    block_plugin_engine bpe_system_test pluginlib
  )
  target_link_libraries(test_01_twist_pipeline yaml-cpp)

  ament_add_gtest(test_02_multi_twist_pipeline
    test/test_02_multi_twist_pipeline.cpp
  )
  target_compile_features(test_02_multi_twist_pipeline PRIVATE cxx_std_20)
  target_compile_definitions(test_02_multi_twist_pipeline PRIVATE
    BPE_TEST_MULTI_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/test_02_multi_twist_pipeline.yaml"
  )
  ament_target_dependencies(test_02_multi_twist_pipeline
    block_plugin_engine bpe_system_test pluginlib geometry_msgs
  )
  target_link_libraries(test_02_multi_twist_pipeline yaml-cpp)
  # Preserve the five legacy integration scenarios against the real BPE
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
  get_filename_component(BPE_TEST_SYSTEM_PREFIX
    "${bpe_system_test_DIR}/../../.." ABSOLUTE)

  set_tests_properties(test_03_engine_integration PROPERTIES ENVIRONMENT
    "AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}:${BPE_TEST_ENGINE_PREFIX}:${BPE_TEST_BOUNDARY_PREFIX}:${BPE_TEST_SYSTEM_PREFIX}:$ENV{AMENT_PREFIX_PATH};LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:${BPE_TEST_ENGINE_PREFIX}/lib:${BPE_TEST_BOUNDARY_PREFIX}/lib:${BPE_TEST_SYSTEM_PREFIX}/lib:$ENV{LD_LIBRARY_PATH};BPE_TEST_CONFIG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/config;BPE_TEST_EXECUTABLE=${BPE_TEST_ENGINE_PREFIX}/lib/block_plugin_engine/block_plugin_engine")
endif()

ament_package()
