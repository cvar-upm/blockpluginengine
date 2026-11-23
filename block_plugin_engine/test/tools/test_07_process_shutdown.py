#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

"""Smoke-test the installed BPE executable's autostart and SIGTERM shutdown."""
import argparse
import os
import selectors
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--config", required=True)
    args = parser.parse_args()

    command = [args.executable, "--ros-args", "-p", "autostart:=true",
               "-p", f"config_file:={args.config}"]
    env = dict(os.environ, RCUTILS_LOGGING_BUFFERED_STREAM="0")
    process = subprocess.Popen(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=env, bufsize=0)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()
    try:
        deadline = time.monotonic() + 8.0
        while b"BlockPluginEngine active" not in output:
            if process.poll() is not None:
                raise AssertionError(f"Engine terminated before activation: {process.returncode}")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError("Engine did not report active state")
            for key, _ in selector.select(timeout=min(remaining, 0.25)):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if chunk:
                    output.extend(chunk)
        process.send_signal(signal.SIGTERM)
        try:
            code = process.wait(timeout=8.0)
        except subprocess.TimeoutExpired as error:
            raise AssertionError("Engine did not exit after SIGTERM") from error
        output.extend(process.stdout.read() or b"")
        if code != 0:
            raise AssertionError(f"Engine exited with status {code}")
        if b"BlockPluginEngine shutdown completed" not in output:
            raise AssertionError("Engine exited without completing lifecycle shutdown")
    except Exception:
        print(output.decode(errors="replace"), file=sys.stderr)
        raise
    finally:
        selector.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdout.close()
    print("Engine activated and exited cleanly after SIGTERM")
    return 0


if __name__ == "__main__":
    sys.exit(main())
