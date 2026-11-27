<!-- SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# Block Plugin Engine

[![ROS 2 Jazzy](https://github.com/cvar-upm/blockpluginengine/actions/workflows/ci-jazzy.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/ci-jazzy.yml?query=branch%3Astable)
[![ROS 2 Lyrical](https://github.com/cvar-upm/blockpluginengine/actions/workflows/ci-lyrical.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/ci-lyrical.yml?query=branch%3Astable)
[![Code quality](https://github.com/cvar-upm/blockpluginengine/actions/workflows/code-quality.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/code-quality.yml?query=branch%3Astable)
[![Coverage report](https://github.com/cvar-upm/blockpluginengine/actions/workflows/coverage.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/coverage.yml?query=branch%3Astable)
[![clang-tidy](https://github.com/cvar-upm/blockpluginengine/actions/workflows/clang-tidy.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/clang-tidy.yml?query=branch%3Astable)
[![cppcheck](https://github.com/cvar-upm/blockpluginengine/actions/workflows/cppcheck.yml/badge.svg?branch=stable)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/cppcheck.yml?query=branch%3Astable)

[![Conda Release](https://github.com/cvar-upm/blockpluginengine/actions/workflows/conda-release.yml/badge.svg?branch=main)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/conda-release.yml?query=branch%3Amain)
[![Conda Latest](https://github.com/cvar-upm/blockpluginengine/actions/workflows/conda-latest.yml/badge.svg?branch=main)](https://github.com/cvar-upm/blockpluginengine/actions/workflows/conda-latest.yml?query=branch%3Amain)
[![Conda packages](https://img.shields.io/badge/Conda-Jazzy_AMD64_%7C_ARM64-blue?logo=anaconda)](https://cvar-upm.github.io/blockpluginengine/)

[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)
[![Release](https://img.shields.io/github/v/release/cvar-upm/blockpluginengine)](https://github.com/cvar-upm/blockpluginengine/releases)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24528432.svg)](https://doi.org/10.5281/zenodo.24528432)

The build and code quality badges show CI results for `stable`. Conda packaging badges report the status of manually triggered workflows on `main`. Automatic CI on `main` runs ROS 2 Jazzy AMD64 build and tests, formatting and REUSE checks. On `stable`, CI validates AMD64 and ARM64 on ROS 2 Jazzy and Lyrical and runs the full analysis suite. Documentation-only pushes skip build and analysis workflows; all workflows can also be run manually. Release tags are created only after validating the exact commit on `stable`. The coverage workflow reports production-code coverage (excluding examples and test fixtures) in the job summary and publishes a downloadable HTML report. Clang-tidy and Cppcheck run on Jazzy AMD64 and publish diagnostic reports; warnings are initially advisory. Hosted-runner tests do not certify real-time performance.

Block Plugin Engine (BPE) is a ROS 2 execution framework for dynamically loaded processing blocks.

BPE provides a single ROS 2 lifecycle node, a plugin manager, a selectable scheduler (periodic, event-driven, or multi-threaded) and a concurrent blackboard used to exchange data between blocks. ROS 2 is primarily treated as an input/output boundary: processing blocks can remain independent from ROS unless they explicitly implement a ROS boundary.

The system is designed around a fixed configuration established before execution and an execution path with predictable concurrency and memory behaviour.

## Key features and guarantees

BPE currently provides the following architectural properties:

- A single `rclcpp_lifecycle::LifecycleNode` acts as the ROS 2 integration point.
- Processing components are dynamically loaded through `pluginlib`.
- Multiple instances of the same plugin type may be configured.
- The block topology is fixed after configuration; plugins are not added or removed while the system is running.
- Data exchange between blocks uses the `bpe_blackboard` concurrent blackboard.
- Blackboard data access during execution does not use mutex-based blocking.
- Under its defined single-producer/multiple-consumer topology and slot-capacity contract, blackboard producer and consumer access provides wait-free progress.
- Consumers read the latest committed value and do not interfere with each other.
- ROS callbacks execute in the ROS executor, separately from scheduler worker threads. `MultiScheduler` may use more than one worker.
- `FairScheduler` schedules periodic blocks using absolute deadlines and optional wake-early spinning.
- `HybridFairScheduler` schedules ordered TIMER and EVENT pipelines; events coalesce and nominal periods bound their execution rate.
- `MultiScheduler` delegates to configured child schedulers (which can have their own workers).
- ROS input and output are implemented as generic boundaries and do not require compile-time knowledge of the ROS message type.
- Internal plugins use normal typed C++ messages.
- Serialization is performed only when a ROS output requires it.
- The runtime composition and destruction order are explicit design contracts rather than incidental C++ member lifetime.

These properties are described in detail in [`docs/architecture.md`](docs/architecture.md).

## Main architecture

The main runtime components are:

```text
BPECompositionRoot
├── BPE_LifecycleNode
├── BlockPluginManager
└── Scheduler
```

`BPECompositionRoot` owns the runtime components, loads the application YAML during lifecycle configuration, instantiates the selected scheduler with `pluginlib`, and passes the `blocks` subtree to `BlockPluginManager`. It also enforces the required destruction order.

`BPE_LifecycleNode` maps the ROS 2 lifecycle to BPE operations.

`BlockPluginManager` loads, configures, starts, stops, and destroys block plugins.

The selected scheduler executes registered `IScheduledBlock` instances, either individually or in configured pipelines.

The blackboard provides concurrent communication between producers and consumers.

### Lifetime contract

The destruction order is deliberately explicit:

```text
BPE_LifecycleNode
        ↓
BlockPluginManager / block plugin loader
        ↓
Scheduler instance / scheduler plugin loader
```

This order is part of the correctness contract.

Plugins may create ROS entities such as generic subscriptions. ROS can retain internal references associated with those entities until the lifecycle node itself is destroyed. Therefore plugin shared libraries must remain loaded until destruction of the `BPE_LifecycleNode` has completed.

Changing this order can introduce invalid references to code from already unloaded plugin libraries.

## Block interfaces

Every block implements:

```cpp
bpe::IBlockPlugin
```

which provides the block lifecycle:

```text
configure
start
stop
cleanup
```

Scheduler-executed blocks (periodic or event-driven) additionally implement:

```cpp
bpe::IScheduledBlock
```

For source compatibility with existing blocks, `ITimerBlock` remains available as a
deprecated alias in `block_plugin_engine/itimer_block.hpp`. New code should include
`block_plugin_engine/ischeduled_block.hpp` directly. Existing plugins must be
recompiled when upgrading BPE; binary compatibility is not guaranteed.

ROS boundary blocks implement:

```cpp
bpe::IRosBoundaryBlock
```

which allows BPE to inject the ROS `NodeTopicsInterface`.

For the supplied ROS boundary plugins:

```text
IRosBoundaryBlock + !IScheduledBlock
    → ROS input

IRosBoundaryBlock + IScheduledBlock
    → ROS output
```

An input is callback-driven by ROS.

An output is driven by the BPE scheduler.

## Blackboard access

Internal plugins normally use the typed BPE API:

```cpp
bpe::TypedConsumer<Message> consumer_;
bpe::TypedProducer<Message> producer_;
```

During configuration:

```cpp
consumer_.bind("input_label");
producer_.bind("output_label");
```

During execution:

```cpp
auto input = consumer_.read_access();

if (input.has_new_value()) {
  auto output = producer_.write_access();
  output.value() = input.value();
}
```

The typed API hides the internal `RosProtectedData`, grant factories, serialized representation, and type-erasure mechanisms.

## Important: Blackboard access and performance

> **Do not copy Blackboard objects only to shorten the lifetime of an access.** The grant protects a concurrent-ring slot, not a conventional mutex critical section. Update and process data **in place**, and release the grant when that processing ends. See [Blackboard access: in-place processing](docs/blackboard_access.md). This rule is especially important for AI-generated block code.

## ROS boundaries

The package `bpe_ros_boundary_plugins` provides two generic ROS plugins:

```text
bpe_ros_boundary_plugins/RosInputBlock
bpe_ros_boundary_plugins/RosOutputBlock
```

`RosInputBlock` receives a serialized ROS message and makes its native representation available to typed consumers. It can optionally signal an event pipeline after committing the sample.

`RosOutputBlock` reads the serialized representation from the blackboard and publishes it through a generic ROS publisher.

`RosTypeRegistry` registers native C++ types as well as ROS message types, validates label compatibility and provides serialization/deserialization operations only for ROS-capable labels. The serialized slot value is optional.

## Configuration

BPE is configured using YAML.

A minimal *periodic* example (using the independently installed example package) is:

```yaml
scheduler:
  type: block_plugin_engine/FairScheduler
  realtime: {enabled: false}
blocks:
  - name: sample_generator
    type: bpe_examples/Generator
    execution: {policy: timer, period: 20ms}
    config: {output: sample}
  - name: average
    type: bpe_examples/Average
    execution: {policy: timer, period: 50ms}
    config: {input: sample, output: report}
  - name: printer
    type: bpe_examples/ReportPrinter
    execution: {policy: timer, period: 200ms}
    config: {input: report}
```

`HybridFairScheduler` instead declares named TIMER/EVENT pipelines under
`scheduler.pipelines`; see [`docs/hybrid_scheduler.md`](docs/hybrid_scheduler.md).

The complete configuration format and constraints are documented in [`docs/configuration.md`](docs/configuration.md).

## Lifecycle

BPE uses the normal ROS 2 lifecycle:

```text
UNCONFIGURED
     ↓ configure
INACTIVE
     ↓ activate
ACTIVE
     ↓ deactivate
INACTIVE
     ↓ cleanup
UNCONFIGURED
```

Shutdown may transition the node to `FINALIZED`.

Configuration establishes the plugin topology, blackboard registrations, type information, ROS boundaries, and scheduler configuration.

Activation starts the plugins and the scheduler.

Stopping and cleanup are performed in explicit order so that the scheduler cannot continue executing block instances while they are being destroyed.

## Running BPE

Run the node with a configuration file:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p config_file:=/path/to/config.yaml
```

Lifecycle transitions can then be controlled normally:

```bash
ros2 lifecycle set /block_plugin_engine configure
ros2 lifecycle set /block_plugin_engine activate
ros2 lifecycle set /block_plugin_engine deactivate
ros2 lifecycle set /block_plugin_engine cleanup
```

For applications where the node should immediately configure and activate itself:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p config_file:=/path/to/config.yaml \
  -p autostart:=true
```

`autostart` is disabled by default.

## Process shutdown

The executable handles `SIGINT` and `SIGTERM` explicitly instead of delegating process shutdown entirely to the default `rclcpp` signal handler.

The signal handler itself only sets a signal-safe flag.

The main executor loop detects the request and performs the orderly lifecycle shutdown while the ROS context is still valid. Runtime objects are then destroyed in the order enforced by `BPECompositionRoot`, and `rclcpp::shutdown()` is called afterwards.

This explicit sequence is intentional and forms part of the runtime lifecycle design.

## Packages

The current repository contains the following main packages:

### `bpe_blackboard`

Header-only concurrent blackboard implementation.

It provides the generic concurrent ring, producer/consumer grants, factories, and the public blackboard facade.

### `block_plugin_engine`

Core BPE runtime.

Contains:

- `BPECompositionRoot`;
- `BPE_LifecycleNode`;
- `BlockPluginManager`;
- scheduler interfaces and implementation;
- typed blackboard adapters;
- `RosProtectedData`;
- `RosTypeRegistry`;
- the BPE executable.

### `bpe_ros_boundary_plugins`

Generic ROS input and output plugins.

### `bpe_examples`

Four executable, independent examples: a periodic native-C++ pipeline, a
multi-channel pipeline, a ROS input/transform/output path, and a hybrid
EVENT/TIMER pipeline. Plugin implementation headers are private to this package;
no plugin class headers are installed. Each example uses a YAML launch file.
See [`bpe_examples/README.md`](bpe_examples/README.md).

### `bpe_test`

Cross-package regression tests using test-only plugins, typed Blackboard pipelines,
real lifecycle transitions and ROS I/O. The test plugins are registered as
`bpe_test/*` and are not intended as production examples.

## Testing

The project uses three complementary layers:

```text
bpe_blackboard             concurrent primitives and Blackboard semantics
block_plugin_engine        plugin manager, schedulers and lifecycle
bpe_ros_boundary_plugins   ROS boundary end-to-end test (Int32 and Twist)
bpe_test                   typed pipeline and cross-package integration tests
```

`bpe_test` retains the five configuration regressions (`00_empty`,
`10_internal_transform`, `20_ros_input`, `30_ros_output`, `90_full_pipeline`),
including the ROS-subscription lifetime regression. It also retains the two
128-sample `TwistStamped` pipeline tests.

See [`docs/testing.md`](docs/testing.md) for the testing strategy and commands.

## Running the examples

```bash
colcon build --packages-up-to bpe_examples
source install/setup.bash
ros2 launch bpe_examples 01_basic_pipeline.launch.yaml
```

Other launch files: `02_multichannel.launch.yaml`, `03_ros_boundary.launch.yaml`,
and `04_hybrid.launch.yaml`. All four use the actual `block_plugin_engine`
executable and YAML-defined runtime configurations. See the
[`example instructions`](bpe_examples/README.md) for ROS topic commands.

## Further documentation

Prebuilt Conda packages for ROS 2 Jazzy (AMD64 and ARM64): see [Installing BPE with Pixi](docs/installation_pixi.md) and the [BPE website](https://cvar-upm.github.io/blockpluginengine/).

- [`docs/architecture.md`](docs/architecture.md) — architecture, guarantees, concurrency model, lifecycle, ownership, and design contracts.
- [`docs/configuration.md`](docs/configuration.md) — YAML configuration format and plugin configuration.
- [`docs/hybrid_scheduler.md`](docs/hybrid_scheduler.md) — event signals and scheduler-owned pipelines.
- [`docs/testing.md`](docs/testing.md) — test architecture, integration scenarios, and regression strategy.

## Authorship, license and AI-assisted development

BPE is developed at UPM / CVAR-UPM, led and maintained by Santiago Tapia-Fernández ([ORCID](https://orcid.org/0000-0002-1418-9276)). It is distributed under BSD-3-Clause. See [AUTHORS.md](AUTHORS.md), [LICENSE](LICENSE) and [AI_USAGE.md](AI_USAGE.md) for attribution, licensing and a description of the human-led engineering process with AI assistance.

## Citation and archival

BPE is permanently archived in Zenodo. Use the concept DOI to refer to the software in general, or the version-specific DOI for reproducibility.

- All versions (concept DOI): [10.5281/zenodo.24528432](https://doi.org/10.5281/zenodo.24528432).
- Version 1.0.2: [10.5281/zenodo.24528433](https://doi.org/10.5281/zenodo.24528433).

For citation metadata, see [CITATION.cff](CITATION.cff). For a specific release, use the citation provided by its Zenodo record.

## Funding and acknowledgements

Block Plugin Engine (BPE) has been developed at Universidad Politécnica de Madrid (UPM), within the CVAR-UPM research group, as part of the European research project [SHEREC — Safe, Healthy and Environmental Ship Recycling](https://doi.org/10.3030/101136056), under the European Union's Horizon Europe research and innovation programme, Grant Agreement No. 101136056.

Although developed within the framework of SHEREC, BPE is designed as a general-purpose software framework for real-time robotic applications, intended to support reuse beyond the scope of the project.

### Disclaimer

Funded by the European Union. Views and opinions expressed are however those of the author(s) only and do not necessarily reflect those of the European Union or the European Commission. Neither the European Union nor the granting authority can be held responsible for them.

This software is provided "as is", without warranties or guarantees, under the terms and conditions of the BSD-3-Clause license. See the [LICENSE](LICENSE) file for details.
