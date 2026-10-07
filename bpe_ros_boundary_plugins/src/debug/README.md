# Temporary ROS input diagnostics

These sources are diagnostic fixtures used to isolate the shutdown fault observed after configuring
`RosInputBlock`.

They deliberately split `RosInputBlock::configure()` into independent operations:

- `DebugRosInputBindBlock`: serialized blackboard/type-registry producer binding only;
- `DebugRosSubscriptionMemberBlock`: generic ROS subscription with a member callback, without blackboard binding;
- `DebugRosSubscriptionFunctionBlock`: the same generic ROS subscription with a callback independent of the plugin instance.

The sources may remain useful as regression fixtures. Once the diagnosis is complete, remove them from
`bpe_ros_boundary_plugins/CMakeLists.txt` and `bpe_ros_boundary_plugins/plugins.xml` unless they are promoted
to permanent tests.
