# Block Plugin Engine testing

## 1. Strategy

Tests live with the functionality they verify. Cross-package integration tests
live in `bpe_test`. Examples are not used as substitute tests.

```text
bpe_blackboard             concurrent primitives, ring and Blackboard contracts
block_plugin_engine        manager, schedulers, lifecycle and composition
bpe_ros_boundary_plugins   generic ROS input/output and event-driven round trip
bpe_test                   typed block pipelines and real-executable integration
```

The dedicated `bpe_system_test` and `bpe_integration_test` packages were retired
after their regression scenarios and test-only plugins were migrated to
`bpe_test`. No `launch_testing` Python launch descriptions are required.

## 2. Package-level tests

### `bpe_blackboard`

Covers concurrent slots and rings, grant ownership and RAII release, label
registration, latest-value and missed-update behaviour, and producer/consumer
stress cases.

```bash
colcon test --packages-select bpe_blackboard --event-handlers console_direct+
```

### `block_plugin_engine`

Covers plugin loading and rollback, scheduler configuration and dispatch,
periodic and event-driven pipelines, simulated clocks, time-related edge cases,
composition-root lifecycle transitions, cleanup and reconfiguration. The
real-time-priority test may require appropriate permissions in the runtime
environment; other simulations use a mocked scheduling policy.

```bash
colcon test --packages-select block_plugin_engine --event-handlers console_direct+
```

### `bpe_ros_boundary_plugins`

Tests the real BPE executable with generic ROS input/output adapters,
`Int32` and `Twist`, and a transform using multi-channel Blackboard adapters.
The test uses `rclpy` as a client and validates round-trip messages and
shutdown. It exercises event-driven pipelines and optional signal injection.

```bash
colcon test --packages-select bpe_ros_boundary_plugins --event-handlers console_direct+
```

## 3. Cross-package integration: `bpe_test`

`bpe_test` owns its own dynamically loaded `TwistStamped` test plugins:

```text
TwistProducerBlock → TwistTransformBlock → TwistCheckerBlock
```

- `test_01_twist_pipeline`: typed producer/transform/checker pipeline over
  128 samples, including sequence and missed-update assertions.
- `test_02_multi_twist_pipeline`: two independent channels using
  `MultiConsumer` and `MultiProducer`, staggered data and 128 samples.
- `test_03_engine_integration.py`: real BPE executable and five YAML fixtures.

### Integration scenarios

| YAML fixture | Coverage |
| --- | --- |
| `00_empty.yaml` | Configure and close an empty engine. |
| `10_internal_transform.yaml` | Configure an internal typed transform. |
| `20_ros_input.yaml` | Configure a ROS subscription and shut down without activating. |
| `30_ros_output.yaml` | Configure a ROS publisher and shut down without activating. |
| `90_full_pipeline.yaml` | Activate input → transform → output; verify `TwistStamped`. |

For the first four scenarios, the client calls the standard ROS 2 lifecycle
`CONFIGURE` service, checks the `INACTIVE` state, and requests clean shutdown.
The fifth uses autostart, checks activation, publishes a `TwistStamped` and
verifies the six transformed components before shutdown.

**The incomplete ROS-input-only case is intentional.** It verifies the lifetime
ordering of a subscription-bearing plugin without requiring an active data
pipeline. The original issue and its diagnostic source files are archived in
[`bpe_ros_boundary_plugins/docs/life_cycle_issues.md`](../bpe_ros_boundary_plugins/docs/life_cycle_issues.md).

```bash
colcon test --packages-select bpe_test --event-handlers console_direct+
```

## 4. Recommended validation

After API, scheduler, lifecycle, plugin loading or ROS boundary changes:

```bash
colcon test --packages-select \
  bpe_blackboard block_plugin_engine bpe_ros_boundary_plugins bpe_test \
  --event-handlers console_direct+
```

To run the full workspace:

```bash
colcon test --event-handlers console_direct+
```

For regressions that existing tests do not localize, first find the smallest
failing configuration, then add capabilities incrementally, keeping shutdown
behaviour constant. Preserve high-value reduced cases as regression tests.
