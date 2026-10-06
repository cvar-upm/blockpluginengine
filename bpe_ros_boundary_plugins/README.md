# bpe_ros_boundary_plugins

This package owns the generic ROS 2 boundary plugins for BlockPluginEngine.

The intended pair is:

- ROS 2 subscription -> BPE blackboard;
- BPE blackboard -> ROS 2 publication.

The package exists already to fix the ownership boundary, but the generic
plugins and their boundary-specific adapters are intentionally deferred until
the serialized/native synchronization design is implemented. The typed ROS 2
boundary blocks currently present in `bpe_system_test` remain
temporary test fixtures and will be replaced by these generic plugins.
