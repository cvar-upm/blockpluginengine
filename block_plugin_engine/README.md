# BlockPluginEngine

## Shared BPE core and blackboard

`block_plugin_engine` now exports a shared library named `block_plugin_engine_core`.
The process-wide BPE blackboard is instantiated in that library rather than in
a header, so all plugins loaded through `pluginlib` use the same object.

The BPE-specific storage is:

```cpp
bpe::RosProtectedData
```

with one native type-erased representation and one ROS 2 serialized
representation. `RosProtectedData` is public because adapters need its
definition, but plugin code should use the supported helpers/adapters instead
of manipulating its members directly.

The concrete BPE blackboard is available as:

```cpp
bpe::BpeBlackboard& bpe::the_blackboard();
```

`BpeBlackboard` uses the default `bpe_blackboard` initialization margin of 2.

### Typed internal-plugin helpers

Internal plugins that know their ROS message type `T` can use:

```cpp
bpe::TypedProducer<T>
bpe::TypedConsumer<T>
```

The endpoint object is declared independently from configuration and is
explicitly bound to a blackboard label during plugin configuration:

```cpp
bpe::TypedProducer<T> producer;
bpe::TypedConsumer<T> consumer;

producer.bind(output_label);
consumer.bind(input_label);
```

`bind()` performs the blackboard registration and automatically supplies the
typed `RosProtectedData` initializer. Binding is mandatory and may only happen
once. Runtime access before binding is rejected. The registration state needed
for deferred configuration is kept inside the helper and is not exposed in the
plugin API.

Runtime access is obtained through RAII adapters:

```cpp
auto output = producer.write_access();
T& value = output.value();

auto input = consumer.read_access();
const T& value = input.value();
```

The adapters own the underlying grant. The producer adapter therefore keeps
exclusive writer ownership for its lifetime, while the consumer adapter keeps
the reader reservation. In this iteration the typed adapters access only the
native representation; native/serialized synchronization is intentionally
deferred.

The typed ROS input/output blocks in `block_plugin_engine_system_test` are
temporary fixtures using the same helpers. They will be replaced by the
generic boundary plugins from `bpe_ros_boundary_plugins`.



## Public configuration dependency

`BlockConfig` is the semantic configuration type exposed by the BPE plugin API.
It is currently an alias of `YAML::Node`.

Because `yaml-cpp` is therefore part of the public API of `block_plugin_engine`,
the package exports `yaml-cpp` as a transitive dependency. A plugin package
should depend on `block_plugin_engine`; it does not need to add `yaml-cpp`
merely because `IBlockPlugin::configure()` uses `BlockConfig`.


## Public vs internal headers

The installed public API is limited to `include/block_plugin_engine/`.
Implementation-only headers live under `src/detail/` and are not installed.

Public API:
- `RosProtectedData`
- `BpeBlackboard` / `the_blackboard()`
- `TypedProducer<T>` / `TypedConsumer<T>`
- `TypedProducerAdapter<T>` / `TypedConsumerAdapter<T>`
- `IBlockPlugin`
- `ITimerBlock`
- `IRosBoundaryBlock`
- `IBlockScheduler`
- `BlockConfig`
- `ExecutionConfig`

Internal implementation:
- `BPE_LifecycleNode`
- `BlockPluginManager`
- `BPE_SimpleScheduler`

`BPE_LifecycleNode` and `BlockPluginManager` are compiled directly into the
engine executable. `BPE_SimpleScheduler` remains a separate shared library.
The process-wide blackboard and other shared runtime state live in
`block_plugin_engine_core`.

## BlockPluginManager configuration

The engine node declares a `config_file` ROS parameter. During `on_configure()`, the
`BlockPluginManager` loads that YAML file, validates the engine-level structure, loads each
`IBlockPlugin` through `pluginlib`, injects supported boundary capabilities, passes only the
`config` subtree to `IBlockPlugin::configure()`, and registers configured execution capabilities
with the scheduler.

Example structure:

```yaml
blocks:
  - name: localization
    type: my_package/LocalizationBlock
    execution:
      policy: timer
      period: 20ms
    config:
      wheel_base: 0.62
```

## Simple Scheduler

La especificación de este SimpleScheduler es:

```text
un único worker thread;
SCHED_FIFO opcional;
prioridad configurable;
CLOCK_MONOTONIC;
clock_nanosleep(..., TIMER_ABSTIME, ...);
despertar anticipado configurable;
busy-wait hasta el deadline;
next_execution += period para conservar fase;
un Block se ejecuta como máximo una vez por pasada;
estructura inmutable mientras está arrancado;
add/remove/clear prohibidos en ejecución;
sin logging en el worker;
sin reservas dinámicas en el loop;
stop() idempotente.
```

La configuración es:

```yaml
scheduler:
  realtime:
    enabled: true
    required: false
    priority: 50

  timing:
    spin_before_deadline: 100us
```

Defaults are applied when fields are omitted.
