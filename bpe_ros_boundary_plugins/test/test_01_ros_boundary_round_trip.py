#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

"""End-to-end ROS boundary test using the real BlockPluginEngine process.

Two ROS message types travel through two EVENT pipelines. Both pipelines invoke
one MultiConsumer/MultiProducer transform instance, and two RosOutputBlocks
publish the results. All scheduler/plugin lifecycle work belongs to the real
engine executable, not to this test.
"""

import os
from pathlib import Path
import signal
import subprocess
import threading
import time

from geometry_msgs.msg import Twist
import pytest
import rclpy
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor
from std_msgs.msg import Int32


STARTUP_TIMEOUT = 12.0
MESSAGE_TIMEOUT = 6.0
SHUTDOWN_TIMEOUT = 8.0


class EngineProcess:
    """Bounded subprocess launcher with non-blocking log collection."""

    def __init__(self, executable: str, config: str):
        self._output = []
        self._active = threading.Event()
        self._lock = threading.Lock()
        self.process = subprocess.Popen(
            [executable, "--ros-args", "-p", "autostart:=true",
             "-p", f"config_file:={config}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env={**os.environ, "RCUTILS_LOGGING_BUFFERED_STREAM": "0"},
        )
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()

    def _read_output(self):
        try:
            for line in self.process.stdout:
                with self._lock:
                    self._output.append(line)
                if "BlockPluginEngine active" in line:
                    self._active.set()
        finally:
            self.process.stdout.close()

    def output(self):
        with self._lock:
            return "".join(self._output)

    def await_active(self):
        deadline = time.monotonic() + STARTUP_TIMEOUT
        while not self._active.is_set():
            if self.process.poll() is not None:
                pytest.fail(f"BPE exited before becoming active ({self.process.returncode}):\n{self.output()}")
            if time.monotonic() >= deadline:
                pytest.fail(f"BPE did not become active:\n{self.output()}")
            self._active.wait(timeout=0.05)

    def stop(self, require_clean=True):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            returncode = self.process.wait(timeout=SHUTDOWN_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            pytest.fail(f"BPE did not stop after SIGTERM:\n{self.output()}")
        self._reader.join(timeout=2.0)
        if require_clean:
            assert returncode == 0, f"BPE exited with {returncode}:\n{self.output()}"
            assert "BlockPluginEngine shutdown completed" in self.output(), (
                f"BPE exited without completing lifecycle shutdown:\n{self.output()}"
            )


def await_condition(predicate, executor, engine, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if engine.process.poll() is not None:
            pytest.fail(f"BPE terminated while waiting for {description}:\n{engine.output()}")
        if predicate():
            return
        executor.spin_once(timeout_sec=0.02)
    pytest.fail(f"Timeout waiting for {description}:\n{engine.output()}")


def test_two_message_types_through_two_event_pipelines():
    executable = os.environ["BPE_BOUNDARY_TEST_EXECUTABLE"]
    config = os.environ["BPE_BOUNDARY_TEST_CONFIG"]
    assert Path(executable).is_file(), executable
    assert Path(config).is_file(), config

    context = Context()
    rclpy.init(context=context)
    node = rclpy.create_node("bpe_boundary_roundtrip_test", context=context)
    executor = SingleThreadedExecutor(context=context)
    executor.add_node(node)
    engine = None
    try:
        int_pub = node.create_publisher(Int32, "/bpe_boundary_test/input_int", 10)
        twist_pub = node.create_publisher(Twist, "/bpe_boundary_test/input_twist", 10)
        int_received = []
        twist_received = []
        int_sub = node.create_subscription(
            Int32, "/bpe_boundary_test/output_int", int_received.append, 10)
        twist_sub = node.create_subscription(
            Twist, "/bpe_boundary_test/output_twist", twist_received.append, 10)

        engine = EngineProcess(executable, config)
        engine.await_active()
        await_condition(
            lambda: int_pub.get_subscription_count() > 0
            and twist_pub.get_subscription_count() > 0
            and int_sub.get_publisher_count() > 0
            and twist_sub.get_publisher_count() > 0,
            executor, engine, STARTUP_TIMEOUT, "ROS publisher/subscriber discovery"
        )

        for round_index in range(2):
            number = Int32(data=10 + round_index * 15)
            twist = Twist()
            twist.linear.x = 1.5 + round_index
            twist.linear.y = 3.0 + round_index
            twist.angular.z = 0.5 + round_index

            # A complete input pair is required by MultiTransformBlock; the
            # second input triggers the transformation, the first may not.
            int_pub.publish(number)
            twist_pub.publish(twist)

            expected_number = number.data + 10
            expected_x = twist.linear.x * 2.0
            expected_y = twist.linear.y
            expected_z = twist.angular.z + 0.25

            await_condition(
                lambda: any(msg.data == expected_number for msg in int_received)
                and any(msg.linear.x == expected_x
                        and msg.linear.y == expected_y
                        and msg.angular.z == expected_z
                        for msg in twist_received),
                executor, engine, MESSAGE_TIMEOUT, f"transformed outputs for pair {round_index + 1}"
            )
            # EVENT pipelines have a 5 ms nominal period. This prevents the
            # next pair being intentionally dropped by their rate limit.
            if round_index == 0:
                time.sleep(0.02)

        engine.stop(require_clean=True)
        engine = None
    finally:
        if engine is not None:
            engine.stop(require_clean=False)
        executor.remove_node(node)
        executor.shutdown()
        node.destroy_node()
        rclpy.shutdown(context=context)
