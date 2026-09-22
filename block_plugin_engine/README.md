# BlockPluginEngine


## Public configuration dependency

`BlockConfig` is the semantic configuration type exposed by the BPE plugin API.
It is currently an alias of `YAML::Node`.

Because `yaml-cpp` is therefore part of the public API of `block_plugin_engine`,
the package exports `yaml-cpp` as a transitive dependency. A plugin package
should depend on `block_plugin_engine`; it does not need to add `yaml-cpp`
merely because `IBlockPlugin::configure()` uses `BlockConfig`.
