#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

case "$(uname -m)" in
    x86_64|aarch64) ;;
    *) echo "Unsupported native architecture: $(uname -m)" >&2; exit 1 ;;
esac

# Only allow local build controls; never accept a different publish destination.
for arg in "$@"; do
    case "$arg" in
        --dry-run|--force|--clean) ;;
        *) echo "Unsupported option: $arg" >&2; exit 2 ;;
    esac
done

extra=()
if [[ -n ${BPE_CONDA_BUILD_PREFIX:-} ]]; then
    [[ $BPE_CONDA_BUILD_PREFIX =~ ^dev_[0-9]+_[0-9]+_$ ]] || exit 2
    extra+=(--build-string-prefix "$BPE_CONDA_BUILD_PREFIX")
fi
exec pixi publish --target-channel "$PWD/conda-channel" "${extra[@]}" "$@"
