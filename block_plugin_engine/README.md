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
