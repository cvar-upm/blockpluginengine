# Commands for manual testing

Run the node:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args -p config_file:=$(ros2 pkg prefix block_plugin_engine_system_test)/share/block_plugin_engine_system_test/config/system_test.yaml
```

In another terminal, activation:

```bash
ros2 lifecycle set /block_plugin_engine configure &
ros2 lifecycle set /block_plugin_engine activate
```

Listen to topic:

```bash
ros2 topic echo /bpe_test/cmd_vel_out
```

In a third terminal, send a message to topic:

```bash
ros2 topic pub --once /bpe_test/cmd_vel_in \
  geometry_msgs/msg/TwistStamped \
  "{header: {frame_id: 'base_link'}, twist: {linear: {x: 1.0, y: 2.0, z: 3.0}, angular: {x: 0.1, y: 0.2, z: 0.3}}}"
```

