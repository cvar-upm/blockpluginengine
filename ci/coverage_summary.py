# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause
"""Render gcovr 7 JSON summary as a GitHub job summary."""
import json
from pathlib import Path
import sys

PACKAGES = ("block_plugin_engine", "bpe_blackboard", "bpe_ros_boundary_plugins")
METRICS = ("line", "function", "branch")


def row(name, files):
    values = []
    for metric in METRICS:
        covered = sum(f[f"{metric}_covered"] for f in files)
        total = sum(f[f"{metric}_total"] for f in files)
        values.append(f"{100 * covered / total:.1f}% ({covered}/{total})" if total else "—")
    return "| " + " | ".join([name, *values]) + " |"


data = json.loads(Path(sys.argv[1]).read_text())
files = data["files"]
if not files or any(Path(f["filename"]).parts[0] not in PACKAGES for f in files):
    raise SystemExit("Unexpected or empty production coverage scope")
print("# BPE production coverage — Jazzy AMD64\n")
print("Examples and test fixtures excluded. GCC exception branches excluded.\n")
print("| Package | Lines | Functions | Branches |\n| --- | ---: | ---: | ---: |")
print(row("**Total**", files))
for package in PACKAGES:
    subset = [f for f in files if Path(f["filename"]).parts[0] == package]
    if not subset:
        raise SystemExit(f"Missing coverage for {package}")
    print(row(package, subset))
