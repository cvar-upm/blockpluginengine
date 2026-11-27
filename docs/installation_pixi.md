<!-- SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# Installing BPE with Pixi

BPE is available as prebuilt Conda packages for ROS 2 Jazzy on Linux AMD64
(`linux-64`) and ARM64 (`linux-aarch64`). Pixi manages the ROS 2 dependencies
through RoboStack and conda-forge. No system-wide ROS 2 installation or BPE
source checkout is required.

## Conda channels

| Channel | Contents |
| --- | --- |
| [`releases`](https://cvar-upm.github.io/blockpluginengine/conda/jazzy/releases/) | Official tagged releases, including previous published versions. Recommended for reproducible deployments. |
| [`latest`](https://cvar-upm.github.io/blockpluginengine/conda/jazzy/latest/) | Most recent published development build from `main`. Replaced when a new development build is published. |

Both channels are hosted on the [BPE website](https://cvar-upm.github.io/blockpluginengine/).
Use one BPE channel per workspace. Development builds have distinct build
identifiers even when their version numbers match an official release.

## Available packages

| Conda package | Contents |
| --- | --- |
| `ros-jazzy-bpe-blackboard` | Concurrent blackboard and C++ headers. |
| `ros-jazzy-block-plugin-engine` | BPE executable, core library and schedulers. |
| `ros-jazzy-bpe-ros-boundary-plugins` | Generic ROS input and output plugins. |

Dependencies between these packages are resolved automatically. Installing the
boundary plugins also installs the engine and blackboard.

## Runtime workspace

Install [Pixi](https://pixi.sh/) and create a directory containing the following
`pixi.toml`:

```toml
[workspace]
name = "bpe-application"
channels = [
    "https://cvar-upm.github.io/blockpluginengine/conda/jazzy/releases",
    "https://prefix.dev/conda-forge",
    "https://prefix.dev/robostack-jazzy",
]
platforms = ["linux-64", "linux-aarch64"]

[dependencies]
ros-jazzy-block-plugin-engine = "==1.0.2"
ros-jazzy-bpe-ros-boundary-plugins = "==1.0.2"
ros-jazzy-ros2cli = "*"
ros-jazzy-ros2run = "*"
```

Install the environment from that directory:

```bash
pixi install
```

The ROS CLI packages are optional if the application does not use `ros2 run`.
For development builds, replace `/jazzy/releases` with `/jazzy/latest` and
select the desired version constraint (for example, `"*"` to follow the
published development version). Use `pixi update` to resolve newer builds.
Keep `pixi.lock` for reproducible environments.

## Workspace for developing C++ plugins

Extend the preceding workspace by adding a C++ toolchain, CMake, Ninja, colcon
and ament. From the workspace directory:

```bash
pixi add cxx-compiler cmake ninja colcon-common-extensions \
    ros-jazzy-ament-cmake
```

For a workspace using the dependencies of the [BPE examples](../bpe_examples/README.md),
add the ROS message and launch packages:

```bash
pixi add ros-jazzy-geometry-msgs ros-jazzy-std-msgs \
    ros-jazzy-launch ros-jazzy-launch-ros ros-jazzy-ros2launch
```

For other applications, add the ROS packages required by their own
`package.xml` and CMake configuration. BPE itself remains an installed binary
underlay; only the application-specific packages need to be built from source.

## Existing Pixi workspaces

Alternatively, add the BPE channel and packages to an existing ROS 2 Jazzy
workspace (which already includes the conda-forge and RoboStack Jazzy channels).
Add the BPE channel URL from the table above to `[workspace].channels` in
`pixi.toml`, then run:

```bash
pixi add 'ros-jazzy-block-plugin-engine==1.0.2' \
    'ros-jazzy-bpe-ros-boundary-plugins==1.0.2'
```

Use matching BPE versions and avoid mixing the official and development
channels in the same environment.
