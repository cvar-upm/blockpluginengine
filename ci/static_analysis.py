# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause
"""Run static analysis against colcon compilation databases."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import logging
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGES = ("block_plugin_engine", "bpe_blackboard", "bpe_examples",
            "bpe_ros_boundary_plugins", "bpe_test")


LOG = logging.getLogger(__name__)


def run_analyzer(command, **kwargs):
    LOG.info("Command: %s", shlex.join(command))
    start = time.monotonic()
    result = subprocess.run(command, **kwargs)
    LOG.info("Return code: %d (%.2f s)", result.returncode, time.monotonic() - start)
    return result


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("jobs must be positive")
    return number


def main():
    logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", choices=("clang-tidy", "cppcheck"))
    parser.add_argument("build", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--jobs", type=positive_int, default=1,
                        help="maximum concurrent clang-tidy processes (default: 1)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.report.mkdir(parents=True, exist_ok=True)
    report = args.report.resolve()
    executable = "clang-tidy-18" if args.tool == "clang-tidy" else "cppcheck"
    LOG.info("Build directory: %s", args.build.resolve())
    LOG.info("Report directory: %s", report)
    if args.tool == "clang-tidy":
        LOG.info("Parallel clang-tidy jobs: %d", args.jobs)
    try:
        version = run_analyzer([executable, "--version"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
        LOG.info("Analyzer version: %s", version.stdout.strip())
    except OSError as exc:
        LOG.warning("Cannot query analyzer version: %s", exc)
    summary = [f"# {args.tool} — Jazzy AMD64", "", "Diagnostic occurrences (not unique defects).", "",
               "| Package | Warnings | Errors | Other | Status |", "| --- | ---: | ---: | ---: | --- |"]
    failed = False
    for package in PACKAGES:
        LOG.info("Analyzing %s", package)
        start = time.monotonic()
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
                def analyze(file):
                    return run_analyzer(["clang-tidy-18", "-p", str(db.parent),
                                         f"--config-file={root / '.clang-tidy'}", file],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

                with (report / f"{package}.txt").open("w") as out:
                    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                        # map preserves file order; only the main thread writes reports.
                        for file, result in zip(files, pool.map(analyze, files)):
                            out.write(f"\n=== {file} ===\n{result.stdout}")
                            for match in re.finditer(r"^.*?:\d+:\d+: (warning|error|fatal error|note):", result.stdout, re.M):
                                counts[match.group(1)] += 1
                            if result.returncode != 0:
                                LOG.error("clang-tidy failed for %s:\n%s", file, result.stdout)
                            bad |= result.returncode != 0
                bad |= bool(counts["error"] or counts["fatal error"])
            else:
                filtered = report / f"{package}.compile_commands.json"
                filtered.write_text(json.dumps(entries))
                xml = report / f"{package}.xml"
                with xml.open("w") as out, (report / f"{package}.txt").open("w") as log:
                    result = run_analyzer(["cppcheck", f"--project={filtered}",
                                             "--enable=warning,performance,portability", "--std=c++20",
                                             "--library=googletest",
                                             f"--library={root / 'ci/cppcheck-pluginlib.cfg'}",
                                             "--inline-suppr", "--xml", "--xml-version=2"],
                                            stdout=log, stderr=out)
                if result.returncode != 0:
                    LOG.error("Cppcheck failed for %s:\n%s\n%s", package,
                              (report / f"{package}.txt").read_text(errors="replace"),
                              xml.read_text(errors="replace"))
                for diagnostic in ET.parse(xml).findall(".//error"):
                    counts[diagnostic.get("severity", "unknown")] += 1
                    if diagnostic.get("severity") == "error" or diagnostic.get("id") in {"internalError", "cppcheckError"}:
                        LOG.error("%s: %s: %s", package, diagnostic.get("id"), diagnostic.get("msg"))
                    bad |= diagnostic.get("id") in {"internalError", "cppcheckError"}
                bad |= result.returncode != 0 or bool(counts["error"])
        except (OSError, ValueError, KeyError, ET.ParseError) as exc:
            LOG.exception("Analysis failed for %s", package)
            (report / f"{package}.failure.txt").write_text(str(exc) + "\n")
            bad = True
        errors = counts["error"] + counts["fatal error"]
        other = sum(counts.values()) - counts["warning"] - errors
        summary.append(f"| {package} | {counts['warning']} | {errors} | {other} | {'FAILED' if bad else 'OK'} |")
        LOG.info("%s: %d warnings, %d errors, %d other; %s (%.2f s)",
                 package, counts["warning"], errors, other,
                 "FAILED" if bad else "OK", time.monotonic() - start)
        failed |= bad
    (report / "summary.md").write_text("\n".join(summary) + "\n")
    LOG.info("Summary:\n%s", "\n".join(summary))
    LOG.info("Result: %s", "FAILED" if failed else "SUCCESS")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
