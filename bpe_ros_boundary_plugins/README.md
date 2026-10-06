# bpe_ros_boundary_plugins

Generic ROS 2 boundary plugins for BlockPluginEngine.

The package provides:

- `bpe_ros_boundary_plugins/RosInputBlock`: generic ROS 2 subscription -> BPE blackboard;
- `bpe_ros_boundary_plugins/RosOutputBlock`: BPE blackboard -> generic ROS 2 publication.

Both plugins operate on `rclcpp::SerializedMessage` and do not know the C++ ROS message type. The
message type is configured with its ROS name (for example `geometry_msgs/msg/TwistStamped`) and is
checked against the type registered by typed BPE plugins for the same blackboard label.

The input boundary is a producer. Its adapter deserializes the received serialized message into the
native object before the producer grant commits. If deserialization fails, the grant is cancelled and
the slot is released without publication.

The output boundary is a consumer. Registering one output for a label marks that label as requiring a
serialized representation. Typed producers serialize their native message before commit only when this
flag is present. A second ROS output for the same blackboard label is rejected during configuration.

## Configuration

Input boundary:

```yaml
- name: ros_input
  type: bpe_ros_boundary_plugins/RosInputBlock
  config:
    topic: /input
    ros_type: geometry_msgs/msg/TwistStamped
    blackboard_output: raw_twist
    qos_depth: 10
```

Output boundary:

```yaml
- name: ros_output
  type: bpe_ros_boundary_plugins/RosOutputBlock
  execution:
    policy: timer
    period: 20ms
  config:
    topic: /output
    ros_type: geometry_msgs/msg/TwistStamped
    blackboard_input: processed_twist
    qos_depth: 10
```
