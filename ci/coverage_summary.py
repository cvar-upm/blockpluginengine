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


def classify(filename):
    parts = Path(filename).parts
    if len(parts) >= 3 and parts[0] in PACKAGES and parts[1] in ("src", "include"):
        return parts[0], False
    for i, part in enumerate(parts[:-3]):
        if (part == "install" and parts[i + 1] in PACKAGES
                and parts[i + 2] == "include"):
            return parts[i + 1], True
    raise SystemExit(f"Unexpected production coverage path: {filename}")


data = json.loads(Path(sys.argv[1]).read_text())
files = data["files"]
if not files:
    raise SystemExit("Empty production coverage scope")
classified = [(f, *classify(f["filename"])) for f in files]
originals = [f for f, _, installed in classified if not installed]
installed_headers = [f for f, _, installed in classified if installed]
for package in PACKAGES:
    if not any(p == package and not installed for _, p, installed in classified):
        raise SystemExit(f"Missing source coverage for {package}")
print("# BPE production coverage — Jazzy AMD64\n")
print("Examples and test fixtures excluded. GCC throw branches excluded.\n")
if installed_headers:
    print("> **Coverage limitation:** gcovr reports installed headers separately from source headers. "
          "The raw total matches the HTML but may count headers twice. The source-only "
          "figures below omit installed-header executions and are not merged coverage.\n")
print("| Package | Lines | Functions | Branches |\n| --- | ---: | ---: | ---: |")
print(row("**Raw total (HTML)**", files))
print(row("**Source-only total**", originals))
for package in PACKAGES:
    print(row(package, [f for f, p, installed in classified if p == package and not installed]))
if installed_headers:
    print(row("Installed headers (separate)", installed_headers))
