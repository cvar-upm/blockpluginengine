# BlockPluginEngine


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
