<!-- SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# Block Plugin Engine configuration

## 1. Runtime parameters

The `block_plugin_engine` executable declares two ROS parameters:

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `config_file` | string | empty | Absolute or resolved path to the BPE YAML configuration. Required before `configure`. |
| `autostart` | bool | `false` | If true, the executable performs lifecycle `configure` and `activate` before entering its executor loop. |

Manual lifecycle control:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p config_file:=/path/to/config.yaml

ros2 lifecycle set /block_plugin_engine configure
ros2 lifecycle set /block_plugin_engine activate
```

Automatic bring-up:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p config_file:=/path/to/config.yaml \
  -p autostart:=true
```

A launch file may instead leave `autostart` false and drive lifecycle transitions explicitly.

## 2. Top-level YAML structure

The current schema contains two top-level sections:

```yaml
scheduler:
  type: block_plugin_engine/FairScheduler
  realtime:
    enabled: true
    required: false
    priority: 50
  timing:
    spin_before_deadline: 100us

blocks:
  - name: block_instance_name
    type: package/PluginClass
    execution:
      policy: timer
      period: 20ms
    config:
      plugin_specific_key: value
```

`blocks` is required and must be a YAML sequence. An empty engine is represented as:

```yaml
blocks: []
```

The `scheduler` map may be omitted; the engine defaults to `block_plugin_engine/FairScheduler`. The composition root loads the selected scheduler, passes it the `scheduler` subtree, then passes only `blocks` to `BlockPluginManager`.

## 3. Scheduler configuration

### 3.1 Real-time settings

```yaml
scheduler:
  realtime:
    enabled: true
    required: false
    priority: 50
```

Defaults:

- `enabled: true`;
- `required: false`;
- `priority: 50`.

When enabled, the scheduler worker attempts to use `SCHED_FIFO`.

- If `required` is false, failure to obtain the requested policy does not prevent startup.
- If `required` is true, scheduler startup fails when real-time setup fails.
- The priority must be within the operating system's valid `SCHED_FIFO` range.

### 3.2 Wake-early timing

```yaml
scheduler:
  timing:
    spin_before_deadline: 100us
```

Default: `100us`.

The value is the interval before the next absolute deadline at which the scheduler leaves `clock_nanosleep()` and starts busy-waiting.

Accepted duration suffixes are:

```text
ns
us
ms
s
```

The value must be non-negative and smaller than every configured timer block period.

### 3.3 Scheduler selection and pipelines

Select a scheduler using `scheduler.type`. The built-in choices are
`block_plugin_engine/FairScheduler`, `block_plugin_engine/HybridFairScheduler`
and `block_plugin_engine/MultiScheduler`. `HybridFairScheduler` uses named
TIMER/EVENT pipelines under `scheduler.pipelines`; its member blocks are
registered by name from `blocks`. See [`hybrid_scheduler.md`](hybrid_scheduler.md)
for the YAML schema and signal semantics. Each scheduler is responsible for
interpreting its own configuration and any per-block `execution` node.

## 4. Block entries

Each block requires:

```yaml
- name: unique_instance_name
  type: package/PluginClass
```

`name` must be non-empty and unique within the process configuration. `type` is the class name exported through `pluginlib` for base class `bpe::IBlockPlugin`.

The optional `config` node is passed unchanged to:

```cpp
IBlockPlugin::configure(const BlockConfig&)
```

where `BlockConfig` is currently an alias of `YAML::Node`.

Example:

```yaml
- name: transform
  type: my_package/MyTransformBlock
  config:
    gain: 2.0
    frame: base_link
```

## 5. Periodic execution

With `FairScheduler`, a block is scheduled only when it has an `execution` section:

```yaml
execution:
  policy: timer
  period: 10ms
```

Current rules:

- the supported per-block policy for `FairScheduler` is `timer`;
- `period` is required and must be greater than zero;
- accepted period suffixes are `ns`, `us`, `ms`, and `s`;
- only blocks implementing `bpe::IScheduledBlock` can be scheduled;
- a block implementing `IScheduledBlock` is not automatically scheduled unless `execution` is present.

For `HybridFairScheduler`, only the pipelines in the scheduler configuration schedule blocks; per-block `execution` is ignored.

This separation is useful for boundary semantics. A ROS input block is callback-driven and has no `execution` section; a ROS output block implements `IScheduledBlock` and runs either periodically or as a member of a hybrid pipeline.

## 6. Generic ROS input boundary

Plugin type:

```text
bpe_ros_boundary_plugins/RosInputBlock
```

Configuration:

```yaml
- name: ros_input
  type: bpe_ros_boundary_plugins/RosInputBlock
  config:
    topic: /input
    ros_type: geometry_msgs/msg/TwistStamped
    blackboard_output: raw_twist
    qos_depth: 10
    # signal: input_ready  # optional, for an EVENT pipeline
```

Fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `topic` | yes | ROS topic name. |
| `ros_type` | yes | Canonical ROS interface name such as `geometry_msgs/msg/TwistStamped`. |
| `blackboard_output` | yes | Blackboard label produced by this boundary. |
| `qos_depth` | no | Keep-last depth; defaults to `10` and must be greater than zero. |
| `signal` | no | Named event signal to emit after committing each incoming message. Requires a scheduler exposing this signal. |

The plugin creates an `rclcpp::GenericSubscription`. Incoming serialized messages are copied into the blackboard slot and deserialized into the native representation before commit.

The boundary may configure before the corresponding typed endpoint, but activation requires the typed serialize/deserialize operations to have been registered for the label.

Do not give a ROS input boundary an `execution` section: it does not implement `IScheduledBlock` and is driven by subscription callbacks on the main executor thread. `IEventSignalUser` is an optional capability: absence of a scheduler provider produces a warning but is not by itself a failure. If a `signal` is explicitly configured and cannot be resolved, the input plugin rejects its own configuration.

## 7. Generic ROS output boundary

Plugin type:

```text
bpe_ros_boundary_plugins/RosOutputBlock
```

Configuration:

```yaml
- name: ros_output
  type: bpe_ros_boundary_plugins/RosOutputBlock
  execution:
    policy: timer
    period: 10ms
  config:
    topic: /output
    ros_type: geometry_msgs/msg/TwistStamped
    blackboard_input: processed_twist
    qos_depth: 10
```

Fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `topic` | yes | ROS topic name. |
| `ros_type` | yes | Canonical ROS interface name. |
| `blackboard_input` | yes | Blackboard label consumed by this boundary. |
| `qos_depth` | no | Keep-last depth; defaults to `10` and must be greater than zero. |

`RosOutputBlock` implements `IScheduledBlock`; with `FairScheduler` it uses a timer `execution` section. With `HybridFairScheduler` it can instead be referenced from `scheduler.pipelines`.

Only one ROS output may be registered for a given blackboard label. A second output is rejected during configuration because the registry models serialized-output requirement as a single label property, not a publisher count.

## 8. Typed internal endpoints

Internal plugins bind either ordinary C++ types or ROS 2 message types to blackboard labels through:

```cpp
bpe::TypedProducer<T>
bpe::TypedConsumer<T>
```

Typical configuration code:

```cpp
input_.bind(config["blackboard_input"].as<std::string>());
output_.bind(config["blackboard_output"].as<std::string>());
```

Binding is one-time. Access before binding or a second bind is rejected.

All endpoints sharing a label must agree on its type identity; the registry rejects mismatches during configuration. Ordinary C++ types are registered without ROS serialization. For ROS-generated messages, conversion operations are registered and serialized storage is only constructed when needed by a ROS boundary.

## 9. Full pipeline example

One regression configuration in `bpe_test` uses the following representative pipeline:

```yaml
scheduler:
  realtime:
    enabled: false
  timing:
    spin_before_deadline: 500us

blocks:
  - name: ros_input
    type: bpe_ros_boundary_plugins/RosInputBlock
    config:
      topic: /bpe_test/input
      ros_type: geometry_msgs/msg/TwistStamped
      blackboard_output: raw_twist
      qos_depth: 10

  - name: twist_transform
    type: bpe_test/TwistTransformBlock
    execution:
      policy: timer
      period: 10ms
    config:
      blackboard_input: raw_twist
      blackboard_output: processed_twist
      linear_x_scale: 2.0
      linear_y_scale: -1.0
      angular_z_offset: 0.25

  - name: ros_output
    type: bpe_ros_boundary_plugins/RosOutputBlock
    execution:
      policy: timer
      period: 10ms
    config:
      topic: /bpe_test/output
      ros_type: geometry_msgs/msg/TwistStamped
      blackboard_input: processed_twist
      qos_depth: 10
```

This produces the execution/data path:

```text
ROS subscription callback (main thread)
    ↓
raw_twist blackboard label
    ↓
TwistTransformBlock (scheduler thread)
    ↓
processed_twist blackboard label
    ↓
RosOutputBlock (scheduler thread)
    ↓
ROS publication
```

## 10. Configuration invariants

The following are deliberate constraints of the current engine:

- block instance names are unique;
- a blackboard label has at most one producer;
- all users of a label share one consistent native or ROS type identity;
- only one ROS output is registered per label;
- scheduler topology does not change while running;
- plugins are loaded/configured before activation;
- execution settings require `IScheduledBlock`;
- type bindings and endpoint factories are considered immutable runtime infrastructure after configuration.

## 11. Runnable examples

For self-contained, non-test examples, see [`bpe_examples/README.md`](../bpe_examples/README.md):

- `01_basic_pipeline.yaml`: native C++ types, independent periodic blocks;
- `02_multichannel.yaml`: combined single-/multi-consumer and producer adapters;
- `03_ros_boundary.yaml`: `PoseStamped` input, `PoseToVelocity` transformation and `Twist` output;
- `04_hybrid.yaml`: two EVENT pipelines with a shared transform and a TIMER monitor.

Each configuration has a corresponding `*.launch.yaml` ROS 2 launch file.
