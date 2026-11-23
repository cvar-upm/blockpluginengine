# Block Plugin Engine architecture

## 1. Goals and scope

Block Plugin Engine (BPE) executes dynamically loaded C++ processing blocks inside one ROS 2 process. The architecture deliberately separates three concerns:

1. **ROS 2 lifecycle and external I/O**;
2. **periodic and event-driven block execution**;
3. **concurrent in-process data exchange**.

ROS 2 is intended primarily as an I/O boundary. Internal processing blocks can remain independent of publishers, subscriptions, executors, and concrete ROS node classes.

The runtime topology is assembled during lifecycle configuration and remains fixed while active.

## 2. System guarantees and design contracts

The table below distinguishes externally useful properties from the internal contracts that preserve them.

| Guarantee / feature | Required contract | Current mechanism |
| --- | --- | --- |
| Steady-state blackboard access uses no mutexes or blocking waits | One producer per label; fixed registered consumer set during execution; sufficient ring capacity | Atomic slot state, CAS acquisition, latest-committed index, structural slot margin |
| Wait-free progress for the configured producer/consumer topology | A writer-accessible slot must always exist; readers share the latest committed slot | Capacity based on consumer count plus a two-slot wait-free margin, rounded to a power of two |
| Consumers observe latest-value semantics | Readers do not reserve historical slots | Every read starts from the latest committed index |
| Runtime topology is immutable | Registration/loading happens before execution | `pluginlib` loading and scheduler registration occur in `configure`; scheduler add/remove/clear reject the running state |
| Periodic execution preserves nominal phase | Deadlines advance from previous deadlines, not from completion time | `next_execution += period` |
| Scheduler wake-up is suitable for low-jitter cyclic work | Absolute clock, optional real-time policy, bounded final spin window | `CLOCK_MONOTONIC`, `TIMER_ABSTIME`, optional `SCHED_FIFO`, wake-early + busy-wait |
| ROS callbacks and cyclic execution are separated | Subscription callbacks must not execute in the cyclic scheduler | Main-thread `SingleThreadedExecutor` plus dedicated scheduler worker |
| Internal blocks need not depend on ROS node types | ROS node access is injected only when requested | `IRosBoundaryBlock::set_ros_interface(NodeTopicsInterface::SharedPtr)` |
| Type-erased ROS boundaries remain type-safe at configuration time | Every blackboard label has one consistent ROS type identity | `RosTypeRegistry` checks `package/msg/Type` identity and provides typed serialization operations |
| Internal-only pipelines do not pay serialization cost | Serialization is required only if a ROS output exists for a label | `RosTypeInfo::requires_serialization()` checked by `TypedProducerAdapter` |
| Failed representation conversion must not publish a partial sample | Producer commit must be cancellable | `ProducerGrant::cancel()` releases the claimed slot without advancing sequence or last-committed index |
| Dynamic plugin code remains loaded while ROS destroys entities created by those plugins | Plugin manager/class loader must outlive the lifecycle node | Explicit `BPECompositionRoot` destruction order: node → plugin manager → scheduler |

### 2.1 Scope of the blackboard real-time guarantee

The blackboard has two phases with different requirements:

- **configuration/registration** may use mutexes and dynamic allocation;
- **steady-state producer/consumer data access** uses the concurrent ring and does not use mutexes or blocking waits.

The wait-free contract applies to the fixed runtime topology. The generic `Blackboard` facade itself uses a mutex when creating labels and registering endpoints; this is intentionally outside the execution path.

### 2.2 Scope of the scheduler real-time guarantee

`FairScheduler` is **real-time-oriented**, not a declaration that the complete process is hard real-time. Its worker loop avoids dynamic scheduler reconfiguration and container growth after start, but the code executed by a plugin's `execute()` remains plugin responsibility. ROS middleware and the operating system also remain outside BPE's deterministic control.

## 3. Top-level composition

`BPECompositionRoot` owns the lifecycle node, plugin manager, the selected
scheduler plugin and its `pluginlib::ClassLoader`. Scheduler selection is
**deferred to lifecycle configuration**, not performed in the root constructor.

```text
BPECompositionRoot(options)
    ├── BPE_LifecycleNode(root, options)
    └── BlockPluginManager(NodeTopicsInterface, logger)

on_configure():
    root reads config_file (YAML)
      → loads and configures scheduler from scheduler: subtree
      → injects actual IBlockScheduler into BlockPluginManager
      → manager configures only blocks: subtree
      → manager registers IScheduledBlock instances with scheduler
```

`BlockPluginManager` does not select a scheduler or read the top-level YAML
file. It passes each block's `execution` node unchanged to
`IBlockScheduler::register_block`; the scheduler interprets that node. The
concrete scheduler is passed directly, without `SchedulerPluginHost`.

The scheduler can optionally implement `IEventSignalProvider`. A block can
optionally implement `IEventSignalUser`. The manager injects the provider
only when both interfaces are present (`dynamic_cast`); a user block with a
non-provider scheduler generates a warning but does not by itself fail
configuration. A block that **requires** a configured signal must reject its
own configuration if no signal handle is available.

`BPE_LifecycleNode` coordinates lifecycle transitions and delegates root
configuration/cleanup to `BPECompositionRoot`.

## 4. Physical lifetime contract

Destruction order is deliberately coded in `BPECompositionRoot::~BPECompositionRoot()`:

```text
BPE_LifecycleNode
    ↓
BlockPluginManager / block plugin loader
    ↓
selected scheduler / scheduler plugin loader
```

This order is a correctness requirement.

During integration testing, a shutdown fault was reduced to a generic ROS subscription created by a dynamically loaded plugin. Destroying `BlockPluginManager` before `BPE_LifecycleNode` allowed the plugin DSO to be unloaded while ROS node teardown still retained subscription-related state. Keeping the manager/class loader alive through lifecycle-node destruction removed the fault.

Therefore:

> **Do not replace the explicit composition-root destruction order with incidental member destruction or move the class loader into an object that can die before the lifecycle node.**

The original diagnostic plugin sources and explanation are archived in [`bpe_ros_boundary_plugins/docs/life_cycle_issues.md`](../bpe_ros_boundary_plugins/docs/life_cycle_issues.md); the former `src/debug/` directory was removed.

## 5. Lifecycle model

`BPE_LifecycleNode` translates ROS lifecycle transitions into BPE operations.

### 5.1 Configure

```text
BPECompositionRoot loads YAML file
    ↓
load and configure selected scheduler plugin
    ↓
inject scheduler into BlockPluginManager
    ↓
BlockPluginManager validates block list
    ↓
load each plugin with pluginlib
    ↓
inject ROS interface into IRosBoundaryBlock and negotiate optional event signals
    ↓
plugin.configure(config subtree)
    ↓
register IScheduledBlock entries with scheduler
```

Configuration failures roll back already configured plugin instances. Shared blackboard/type-registry state is reset when engine-level plugin configuration fails.

### 5.2 Activate

```text
start plugins in configuration order
    ↓
start scheduler
```

If a plugin start fails, previously started plugins are stopped. If scheduler start fails, plugins are stopped before returning failure.

### 5.3 Deactivate

```text
stop scheduler and join worker
    ↓
stop plugins in reverse order
```

The scheduler stops first so no `IScheduledBlock::execute()` call can race with plugin stop.

### 5.4 Cleanup

```text
ensure scheduler stopped
    ↓
cleanup plugins in reverse order and clear their scheduler references
    ↓
destroy plugin instances
    ↓
release scheduler instance
    ↓
reset process-wide blackboard
    ↓
reset ROS type registry
```

After cleanup the engine can be configured again from an empty runtime state.

### 5.5 Shutdown

Shutdown performs the same operational safety sequence needed to terminate from any configured/active state: stop scheduler, stop plugins, clean up plugin instances, then reset shared runtime state.

Operational lifecycle teardown and physical C++ object lifetime are intentionally separate concepts. Lifecycle callbacks make the system inactive/clean; `BPECompositionRoot` then guarantees safe object destruction order.

## 6. Process shutdown model

The executable does not install rclcpp's default signal handler. It initializes ROS with `SignalHandlerOptions::None` and installs minimal SIGINT/SIGTERM handlers that only set a `sig_atomic_t` flag.

The main thread then follows an explicit sequence:

```text
SingleThreadedExecutor::spin_once() loop
    ↓
signal flag observed
    ↓
remove lifecycle node from executor
    ↓
request lifecycle shutdown while ROS context is still valid
    ↓
destroy executor
    ↓
leave BPECompositionRoot scope
    ↓
root destroys node → manager → scheduler
    ↓
rclcpp::shutdown()
```

This makes process-level shutdown ordering visible and testable.

## 7. Execution and threading model

BPE itself manages two execution domains when periodic blocks are present:

```text
Main thread
  └── rclcpp::SingleThreadedExecutor
      └── ROS callbacks, including RosInputBlock subscriptions

Scheduler worker thread(s)
  ├── FairScheduler: periodic IScheduledBlock::execute()
  ├── HybridFairScheduler: TIMER/EVENT pipelines
  └── MultiScheduler: child scheduler workers
```

The number of worker threads depends on the selected scheduler and its configuration.

ROS/DDS may use additional internal threads; those are middleware implementation details and are not BPE scheduling domains.

This split has an important semantic consequence:

- a ROS input boundary is callback-driven and does **not** implement `IScheduledBlock`;
- a ROS output boundary is scheduler-driven and **does** implement `IScheduledBlock`.

The same pattern can be applied to future non-ROS boundaries: reception may be externally driven, while deterministic emission can be scheduled.

## 8. Scheduler design

BPE loads `FairScheduler` by default when `scheduler.type` is absent. The
configured scheduler is a `pluginlib` implementation of `IBlockScheduler`.

- **`FairScheduler`**: one worker, individual periodic block execution based
  on per-block `execution` YAML.
- **`HybridFairScheduler`**: one worker, ordered named TIMER or EVENT pipelines;
  pipeline configuration resides under `scheduler.pipelines`. A signal marks
  work eligible but does not carry payload data. Events can coalesce and
  pipelines are subject to their nominal periods.
- **`MultiScheduler`**: coordinates configured child schedulers and can place
  work on multiple scheduler threads. Signal discovery across children must
  have an unambiguous provider.

See [`hybrid_scheduler.md`](hybrid_scheduler.md) for the hybrid schedule,
priority, signal and coalescing semantics.

### 8.1 FairScheduler timing

`FairScheduler` is a single-worker cyclic scheduler.

### 8.1 Timing mechanism

For the earliest pending deadline it:

1. computes `deadline - spin_before_deadline`;
2. sleeps using `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`;
3. busy-waits until the absolute deadline;
4. executes each block whose deadline has been reached;
5. advances that block's deadline with `next_execution += period`.

Using absolute deadlines and incrementing from the previous deadline avoids systematic phase drift caused by execution time.

### 8.2 Real-time policy

The worker can request `SCHED_FIFO` with configurable priority. Real-time setup may be optional or required:

- `enabled: false`: do not request `SCHED_FIFO`;
- `enabled: true, required: false`: attempt real-time scheduling but continue if the OS rejects it;
- `enabled: true, required: true`: scheduler start fails if `SCHED_FIFO` cannot be established.

### 8.3 Runtime immutability

Scheduler topology changes and `clear` are rejected while running. Scheduling structures are constructed before start. `stop()` is idempotent and joins the worker thread before returning.

The scheduler worker contains no logging and does not grow its containers in the cyclic loop. This does not constrain allocations performed inside plugin `execute()` implementations.

## 9. Block model

### 9.1 `IBlockPlugin`

Every plugin implements:

```cpp
configure(const BlockConfig&)
start()
stop()
cleanup()
```

`BlockConfig` is currently a semantic alias for `YAML::Node`.

### 9.2 `IScheduledBlock`

A plugin implementing `IScheduledBlock` may have an `execution:` section in YAML and is registered with the scheduler. A block that declares execution settings without implementing `IScheduledBlock` is rejected during configuration.

### 9.3 `IRosBoundaryBlock`

A plugin implementing `IRosBoundaryBlock` receives a shared `NodeTopicsInterface` from the plugin manager before `configure()` is called. The interface is passed by const reference to a `shared_ptr`, expressing shared dependency rather than transfer of ownership.

## 10. Blackboard architecture

### 10.1 Generic layer

`bpe_blackboard` is a header-only C++20 library in namespace `bpe::lfbb`. Its core layers are:

```text
Blackboard<ProtectedData>
    ↓ labels / registration
ConcurrentRing<ProtectedData>
    ↓
ConcurrentSlot<ProtectedData>

ProducerGrantFactory → ProducerGrant
ConsumerGrantFactory → ConsumerGrant
```

`ConcurrentSlot` separates concurrency state from the protected payload. Slot state represents free, writer-owned, or reader-count ownership using atomics and CAS.

### 10.2 Runtime ring contract

Each label supports:

- at most one registered producer;
- multiple registered consumers;
- one active grant per endpoint factory;
- latest-committed read semantics;
- monotonically increasing commit sequence;
- timestamp captured at commit;
- consumer status: `no_data_yet`, `no_update`, `ok`, or `missed`.

A producer grant normally commits on destruction. `cancel()` releases the write claim without incrementing sequence, changing the latest committed index, or publishing the modified slot.

### 10.3 In-place access is an essential performance property

> **IMPORTANT: Do not copy protected payloads merely to release a Blackboard access earlier.** Producer and consumer grants reserve independent ring slots; they are not conventional mutex locks. Perform the necessary computation **in place** while holding the grant. A producer publishes on grant destruction, and a reader retains a stable committed value while its grant exists. Artificial copy/modify/copy-back patterns increase work and latency.
>
> See [Blackboard access guidance](blackboard_access.md), especially when generating block code automatically.

### 10.3 Capacity rule

The ring reserves a structural margin of two slots beyond the consumer count:

```text
base_capacity = max(4, consumer_count + 2)
capacity = next_power_of_two(base_capacity + uninitialized_registration_margin)
```

The two structural slots are independent of the initialization margin:

- one accounts for the latest committed/readable position;
- one guarantees writer progress under the fixed consumer topology.

The public `Blackboard` default initialization margin is `2`. It allows a limited number of registrations without a new initializer after storage has been initialized.

### 10.4 BPE process-wide instance

BPE specializes the generic blackboard as:

```cpp
using BpeBlackboard = lfbb::Blackboard<RosProtectedData>;
BpeBlackboard& the_blackboard();
```

The instance lives in the shared `block_plugin_engine_core` library rather than in a header. This guarantees that the engine executable and every plugin DSO observe the same process-wide blackboard.

## 11. Native and serialized representations

`RosProtectedData` is currently the engine's blackboard slot payload. Its
name is historical: **native-only C++ objects are supported**, independently
of ROS 2 message generation or ROS type support.

```cpp
struct RosProtectedData {
  std::shared_ptr<void> native;
  std::optional<rclcpp::SerializedMessage> serialized;
};
```

Only boundary adapters should access the serialized representation directly.
For native-only labels the optional serialized value remains disengaged.
ROS message types also remain native unless a ROS input or output requires
serialized data; a typed producer serializes when the registry indicates an
associated ROS output. Failed conversion cancels the write grant, avoiding a
partial published sample.

### 11.1 Typed endpoint registration

`TypedProducer<T>` and `TypedConsumer<T>` provide the same public API for
native C++ and ROS-generated message types. The internal `register_data_type<T>`
uses `rosidl_generator_traits::is_message<T>` at compile time:

- **ROS message:** register its canonical ROS name and type-erased
  serialization/deserialization operations;
- **ordinary C++ type:** register a native type identity without ROS
  serialization support.

Bindings are completed during configuration and verified against existing
registrations for the label. A type mismatch on a shared label is rejected.
`MultiConsumer` and `MultiProducer` use the same typed facilities.

### 11.2 ROS boundaries

`RosTypeRegistry` remains the name of the registry but also stores identities
for native types. It validates labels shared between typed endpoints and ROS
boundaries. For ROS input, a generic subscription supplies serialized bytes;
the boundary deserializes them into the native object before commit. For ROS
output, a generic publisher consumes the serialized representation when a new
sample is available.

A configured `RosInputBlock` can optionally request a named `signal` from
`IEventSignalProvider` and emit it **after** committing its Blackboard update.
Without `signal`, the block works with a periodic `FairScheduler` even if no
provider is available. A configured signal requires a scheduler providing the
named handle (for example `HybridFairScheduler`).

### 11.3 Future serialization formats

The typed endpoints are independent of the transport. Supporting another
serialization format would require extending the internal registry and
representation/transport adapters, but need not change `TypedProducer<T>` or
`TypedConsumer<T>`. There is currently no generic multi-format serializer API.

## 12. Configuration-time versus runtime invariants

The following operations belong to configuration and are allowed to lock or allocate:

- YAML parsing;
- plugin loading;
- endpoint registration;
- blackboard growth and slot initialization;
- ROS publisher/subscription construction;
- scheduler-vector construction;
- ROS type-registry completion.

After activation, the structural model is fixed. Runtime code should treat plugin instances, labels, endpoint factories, scheduler entries, and type-registry entries as immutable infrastructure.

## 13. Public versus internal API

Installed public headers under `include/block_plugin_engine/` contain the interfaces required by plugin packages:

- `IBlockPlugin`;
- `IScheduledBlock`;
- `IRosBoundaryBlock`;
- `IBlockScheduler` and optional event-signal interfaces;
- `BlockConfig`;
- `TypedProducer<T>` / `TypedConsumer<T>` and adapters;
- `RosProtectedData`, `BpeBlackboard`, and `RosTypeRegistry` support.

Engine composition classes live under `src/detail/` and are not installed:

- `BPECompositionRoot`;
- `BPE_LifecycleNode`;
- `BlockPluginManager`;
- `FairScheduler` implementation details.

The separation is intentional: plugin authors depend on behavioral interfaces, not on the engine's composition root or node implementation.
