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
endif()

ament_package()
