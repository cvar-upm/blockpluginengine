# bpe_test

Cross-package regression tests for Block Plugin Engine. The test-only
`TwistStamped` plugins are implemented and registered in this package as
`bpe_test/*`; no separate legacy test packages are required.

## Tests

- `test_01_twist_pipeline` checks a typed producer → transformer → checker
  pipeline over 128 samples, including sequence and missed-update checks.
- `test_02_multi_twist_pipeline` checks two channels with `MultiConsumer` and
  `MultiProducer`, staggered inputs and 128 samples.
- `test_03_engine_integration.py` starts the real BPE executable and checks
  five external YAML configurations:
  - `00_empty`: configure and clean shutdown without blocks;
  - `10_internal_transform`: configure an internal typed block;
  - `20_ros_input`: configure and destroy a generic ROS subscription without
    activating (lifetime regression);
  - `30_ros_output`: configure and destroy a generic ROS publisher;
  - `90_full_pipeline`: activate ROS input → typed transform → ROS output,
    validate all six `TwistStamped` components and clean shutdown.

The first two tests are C++ GoogleTest. The integration scenarios use Python
`pytest` and `rclpy` as test clients, **not** Python launch files. YAML remains
the source of BPE configuration.

## Run

```bash
colcon build --packages-up-to bpe_test
colcon test --packages-select bpe_test --event-handlers console_direct+
colcon test-result --test-result-base build/bpe_test --verbose
```

The ROS subscription-only configuration is intentionally not activated: it
regresses the lifetime problem diagnosed in
`bpe_ros_boundary_plugins/docs/life_cycle_issues.md`.

## Concurrent Blackboard scenarios

`test_05_multischeduler_blackboard_concurrency` is the baseline with four
scheduler threads: one TIMER writer, an EVENT reader and two independent TIMER
readers. It defaults to 10,000 writes, a 2000 us writer period, and 1000 us
EVENT/TIMER reader periods. The minimum coverage requirements are 90% (EVENT)
and 80% (each TIMER reader).

`test_06_multischeduler_blackboard_high_frequency` builds **the same C++ source**
with a more demanding default configuration: 200 us writer period and 100 us
EVENT/TIMER periods. In this stress scenario the minimum distinct-version
coverage is 50% per reader, because Blackboard guarantees the latest consistent
snapshot, not delivery of every version. Both scenarios still require zero
payload integrity errors, increasing observed sequences and observation of the
final version by all three readers. The test prints each reader's received
count and percentage.

Both tests accept the following environment variables (all period values are
positive **integer microseconds**, between 50 and 1,000,000):

| Variable | test_05 default | test_06 default |
| --- | ---: | ---: |
| `BPE_CONCURRENCY_WRITER_US` | 2000 | 200 |
| `BPE_CONCURRENCY_EVENT_US` | 1000 | 100 |
| `BPE_CONCURRENCY_TIMER_US` | 1000 | 100 |
| `BPE_CONCURRENCY_WRITES` | 10000 | 10000 |

The write count accepts values between 10,000 and 1,000,000. The two TIMER
readers intentionally share the same *period*, but run on different scheduler
threads. The supervisor has its own consumer and does not occupy a scheduler
worker. A 75-second CTest timeout applies by default; increasing the write
count or period substantially may require a larger timeout.

Example (run the test binary directly to supply environment overrides without
CTest's test-specific environment):

```bash
BPE_CONCURRENCY_WRITER_US=500 \
BPE_CONCURRENCY_EVENT_US=250 \
BPE_CONCURRENCY_TIMER_US=250 \
./build/bpe_test/test_06_multischeduler_blackboard_high_frequency
```
