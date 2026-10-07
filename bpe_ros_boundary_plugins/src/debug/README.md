# Archived ROS input diagnostic fixtures

These sources were created while diagnosing a shutdown fault triggered after configuring
`RosInputBlock`. They split the input boundary into independent operations:

- `DebugRosInputBindBlock`: serialized blackboard/type-registry producer binding only;
- `DebugRosSubscriptionMemberBlock`: generic ROS subscription with a member callback, without blackboard binding;
- `DebugRosSubscriptionFunctionBlock`: the same generic ROS subscription with a callback independent of the plugin instance.

The experiments showed that producer binding alone shut down correctly, while either generic
subscription variant reproduced the fault. A later lifetime experiment showed that keeping the
`BlockPluginManager` (and therefore its plugin `ClassLoader`) alive until after destruction of the
`BPE_LifecycleNode` removed the fault. The permanent fix is ownership by `BPECompositionRoot`, whose
explicit destruction order keeps plugin DSOs loaded while ROS destroys the lifecycle node.

These files are intentionally **not compiled and not registered in `plugins.xml`**. They are retained
only as diagnostic fixtures in case the lifetime issue must be investigated again. Do not add them
back to the production plugin library unless they are explicitly needed for diagnostics.
