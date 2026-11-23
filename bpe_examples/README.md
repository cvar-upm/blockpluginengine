# BPE examples

Four independent applications using the **real** `block_plugin_engine` executable.
The example plugins are private implementation details (`src/*.cpp`); the package
exports **no C++ plugin headers**. Types in `src/types.hpp` are private too.
No tests or test plugins are reused.

## Build and launch

```bash
colcon build --packages-up-to bpe_examples
source install/setup.bash
ros2 launch bpe_examples 01_basic_pipeline.launch.yaml
```

Substitute `02_multichannel`, `03_ros_boundary`, or `04_hybrid` for the filename.
The example configurations are installed under `share/bpe_examples/config/`.
All schedulers disable real-time priority to run without `CAP_SYS_NICE`.
Stop an example with Ctrl+C.

You can also invoke BPE directly, for example:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p autostart:=true \
  -p config_file:="$(ros2 pkg prefix bpe_examples)/share/bpe_examples/config/01_basic_pipeline.yaml"
```

## 01 — Basic pipeline

A periodic generator creates `Sample{sequence, vector<double>}` every 20 ms.
`Average` consumes the latest sample every 50 ms and produces a `Report`
containing a sequence number and mean. A printer runs every 200 ms, but only prints
when `has_new_value()` is true. This intentionally illustrates sample skipping.

## 02 — Multichannel

Two generators produce independently. Four interface combinations are visible:

| Block | Input | Output |
| --- | --- | --- |
| Generator | none | TypedProducer<Sample> |
| FuseSamples | MultiConsumer<Sample, Sample> | TypedProducer<Report> |
| SplitReport | TypedConsumer<Report> | MultiProducer<Report, Summary> |
| PairAnnotator | MultiConsumer<Report, Summary> | MultiProducer<Report, Summary> |

The two printers show the outputs. `Summary` contains a `std::vector<double>`. The only nontrivial C++ member type used in these examples is `std::vector`.
The fusion and annotator remember independently changed values using
`has_new_value()` and only publish once both sides have received updates.

## 03 — ROS boundary and transformation

`/bpe_examples/pose` (`geometry_msgs/msg/PoseStamped`) → ROS input adapter →
`PoseToVelocity` → ROS output adapter → `/bpe_examples/velocity`
(`geometry_msgs/msg/Twist`). The transform multiplies x/y position by 0.5 and
clamps each resulting linear velocity to [-1, 1] m/s.

In a second terminal (source the workspace):

```bash
ros2 topic echo /bpe_examples/velocity
```

In another terminal:

```bash
ros2 topic pub --once /bpe_examples/pose geometry_msgs/msg/PoseStamped \
  '{header: {frame_id: map}, pose: {position: {x: 3.0, y: -1.0, z: 0.0}, orientation: {w: 1.0}}}'
```

Expected output contains `linear.x: 1.0` and `linear.y: -0.5`.

## 04 — Hybrid event + timer scheduling

Two input adapters emit separate named signals. Both EVENT pipelines run
**the same** `CommandMixer` instance, followed by two ROS outputs. The mixer
consumes `Int32` and `Twist`; it only publishes once both have changed. It
replaces Twist `linear.x` by `speed * 0.1` and derives an alarm level (1 if
speed > 5, else 0). A separate 500 ms TIMER pipeline runs `CommandMonitor`.

Watch the results:

```bash
ros2 topic echo /bpe_examples/command
ros2 topic echo /bpe_examples/alarm
```

Send **both** inputs (one command in each terminal):

```bash
ros2 topic pub --once /bpe_examples/speed std_msgs/msg/Int32 '{data: 8}'
ros2 topic pub --once /bpe_examples/direction geometry_msgs/msg/Twist \
  '{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.3}}'
```

Expected `linear.x: 0.8`, `angular.z: 0.3`, `alarm.data: 1`.
Send a second pair to observe fresh output. Signals are only triggers; data
resides in Blackboard. Events can coalesce, and the transform checks which
inputs have actually changed. The periodic monitor is independent of inputs.
