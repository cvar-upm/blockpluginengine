# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause
"""Run static analysis against colcon compilation databases."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

PACKAGES = ("block_plugin_engine", "bpe_blackboard", "bpe_examples",
            "bpe_ros_boundary_plugins", "bpe_test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", choices=("clang-tidy", "cppcheck"))
    parser.add_argument("build", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.report.mkdir(parents=True, exist_ok=True)
    report = args.report.resolve()
    summary = [f"# {args.tool} — Jazzy AMD64", "", "Diagnostic occurrences (not unique defects).", "",
               "| Package | Warnings | Errors | Other | Status |", "| --- | ---: | ---: | ---: | --- |"]
    failed = False
    for package in PACKAGES:
        counts = Counter()
        bad = False
        try:
            db = (args.build / package / "compile_commands.json").resolve()
            entries = json.loads(db.read_text())
            if not entries:
                raise ValueError("Empty compilation database")
            # Exclude generated/external translation units, not BPE tests.
            entries = [e for e in entries if (Path(e["directory"]) / e["file"]).resolve().is_relative_to(root / package)]
            if not entries:
                raise ValueError("No project translation units")
            if args.tool == "clang-tidy":
                files = sorted({str((Path(e["directory"]) / e["file"]).resolve()) for e in entries})
                with (report / f"{package}.txt").open("w") as out:
                    for file in files:
                        result = subprocess.run(["clang-tidy-18", "-p", str(db.parent),
                                                 f"--config-file={root / '.clang-tidy'}", file],
                                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                        out.write(f"\n=== {file} ===\n{result.stdout}")
                        for match in re.finditer(r"^.*?:\d+:\d+: (warning|error|fatal error|note):", result.stdout, re.M):
                            counts[match.group(1)] += 1
                        bad |= result.returncode != 0
                bad |= bool(counts["error"] or counts["fatal error"])
            else:
                filtered = report / f"{package}.compile_commands.json"
                filtered.write_text(json.dumps(entries))
                xml = report / f"{package}.xml"
                with xml.open("w") as out, (report / f"{package}.txt").open("w") as log:
                    result = subprocess.run(["cppcheck", f"--project={filtered}",
                                             "--enable=warning,performance,portability", "--std=c++20",
                                             "--library=googletest",
                                             f"--library={root / 'ci/cppcheck-pluginlib.cfg'}",
                                             "--inline-suppr", "--xml", "--xml-version=2"],
                                            stdout=log, stderr=out)
                for diagnostic in ET.parse(xml).findall(".//error"):
                    counts[diagnostic.get("severity", "unknown")] += 1
                    bad |= diagnostic.get("id") in {"internalError", "cppcheckError"}
                bad |= result.returncode != 0 or bool(counts["error"])
        except (OSError, ValueError, KeyError, ET.ParseError) as exc:
            (report / f"{package}.failure.txt").write_text(str(exc) + "\n")
            bad = True
        errors = counts["error"] + counts["fatal error"]
        other = sum(counts.values()) - counts["warning"] - errors
        summary.append(f"| {package} | {counts['warning']} | {errors} | {other} | {'FAILED' if bad else 'OK'} |")
        failed |= bad
    (report / "summary.md").write_text("\n".join(summary) + "\n")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
