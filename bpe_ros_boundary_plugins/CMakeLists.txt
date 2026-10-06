cmake_minimum_required(VERSION 3.8)
project(bpe_ros_boundary_plugins)

find_package(ament_cmake REQUIRED)
find_package(block_plugin_engine REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)

# Generic ROS 2 input/output boundary plugins will be added in the next
# iteration. This package is created now so their public ownership and
# dependency boundary are fixed before serialization support is introduced.

ament_package()
