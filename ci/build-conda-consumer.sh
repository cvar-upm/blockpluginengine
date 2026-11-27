#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
root="$PWD"
# Historical tags may predate Pixi. Overlay packaging files only, never C++.
cp tooling/pixi.toml source/pixi.toml
mkdir -p source/ci
cp tooling/ci/build-conda.sh source/ci/
for pkg in bpe_blackboard block_plugin_engine bpe_ros_boundary_plugins; do
    cp "tooling/$pkg/pixi.toml" "source/$pkg/pixi.toml"
    cp source/LICENSES/BSD-3-Clause.txt "source/$pkg/BSD-3-Clause.txt"
    sed -i '/^publish = true$/a license = "BSD-3-Clause"\nlicense-file = "BSD-3-Clause.txt"' "source/$pkg/pixi.toml"
done
cd source
if [[ "$MODE" == latest ]]; then
    export BPE_CONDA_BUILD_PREFIX="dev_${GITHUB_RUN_ID}_${GITHUB_RUN_ATTEMPT}_"
fi
bash ci/build-conda.sh
shopt -s nullglob
packages=(conda-channel/"$PLATFORM"/*.conda)
[[ ${#packages[@]} == 3 ]]
# Fail before publication if license metadata is missing.
python3 - "${packages[@]}" <<'EOF'
import sys, zipfile
for path in sys.argv[1:]:
    with zipfile.ZipFile(path) as z:
        info = next(n for n in z.namelist() if n.startswith('info-'))
        import subprocess
        data = subprocess.check_output(['tar', '--zstd', '-tf', '-'], input=z.read(info))
        assert any(n.startswith('info/licenses/') and not n.endswith('/') for n in data.decode().splitlines()), path
EOF
consumer=$(mktemp -d)
mkdir -p "$consumer/src"
cp -a bpe_examples "$consumer/src/"
cat > "$consumer/pixi.toml" <<EOF
[workspace]
name = "bpe-consumer"
channels = ["file://$PWD/conda-channel", "https://prefix.dev/conda-forge", "https://prefix.dev/robostack-jazzy"]
platforms = ["$PLATFORM"]
[dependencies]
ros-jazzy-bpe-ros-boundary-plugins = "==$VERSION"
ros-jazzy-block-plugin-engine = "==$VERSION"
ros-jazzy-bpe-blackboard = "==$VERSION"
cxx-compiler = "*"
cmake = "*"
ninja = "*"
colcon-common-extensions = "*"
ros-jazzy-ament-cmake = "*"
ros-jazzy-geometry-msgs = "*"
ros-jazzy-std-msgs = "*"
ros-jazzy-launch = "*"
ros-jazzy-launch-ros = "*"
ros-jazzy-ros2launch = "*"
ros-jazzy-rclpy = "*"
ros-jazzy-ros2cli = "*"
ros-jazzy-ros2pkg = "*"
ros-jazzy-ros2run = "*"
EOF
cd "$consumer"
pixi install
pixi run colcon build --packages-select bpe_examples --cmake-args -G Ninja -DCMAKE_BUILD_TYPE=Release
pixi run bash -c 'source install/local_setup.bash; python "$1"' _ "$root/tooling/ci/validate_artifact.py"
