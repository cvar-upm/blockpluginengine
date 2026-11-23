#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

"""Exercise five lifecycle and integration scenarios using the real BPE executable.

Four configure-only scenarios stop from INACTIVE; the fifth asserts the
TwistStamped transformation using a live ROS publisher and subscription.
"""

import os
from pathlib import Path
import signal
import subprocess
import threading
import time

from geometry_msgs.msg import TwistStamped
from lifecycle_msgs.msg import State, Transition
from lifecycle_msgs.srv import ChangeState, GetState
import pytest
import rclpy
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor

STARTUP_TIMEOUT = 12.0
MESSAGE_TIMEOUT = 6.0
SHUTDOWN_TIMEOUT = 8.0


class EngineProcess:
    def __init__(self, config, autostart):
        executable = os.environ["BPE_TEST_EXECUTABLE"]
        assert Path(executable).is_file(), executable
        assert Path(config).is_file(), config
        self.lines = []
        self.lock = threading.Lock()
        self.active = threading.Event()
        self.process = subprocess.Popen(
            [executable, "--ros-args", "-p", f"autostart:={str(autostart).lower()}",
             "-p", f"config_file:={config}"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1,
            env={**os.environ, "RCUTILS_LOGGING_BUFFERED_STREAM": "0"},
        )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            for line in self.process.stdout:
                with self.lock:
                    self.lines.append(line)
                if "BlockPluginEngine active" in line:
                    self.active.set()
        finally:
            self.process.stdout.close()

    def output(self):
        with self.lock:
            return "".join(self.lines)

    def check_alive(self, description):
        if self.process.poll() is not None:
            pytest.fail(f"BPE exited while {description} ({self.process.returncode}):\n{self.output()}")

    def await_active(self):
        deadline = time.monotonic() + STARTUP_TIMEOUT
        while not self.active.wait(0.05):
            self.check_alive("waiting for ACTIVE")
            if time.monotonic() >= deadline:
                pytest.fail(f"BPE did not activate:\n{self.output()}")

    def stop(self, require_clean=True):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            code = self.process.wait(timeout=SHUTDOWN_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            pytest.fail(f"BPE did not terminate after SIGTERM:\n{self.output()}")
        self.reader.join(timeout=2)
        if require_clean:
            assert code == 0, f"BPE returned {code}:\n{self.output()}"
            assert "BlockPluginEngine shutdown completed" in self.output(), self.output()


def spin_until(executor, engine, condition, description, timeout=STARTUP_TIMEOUT):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        engine.check_alive(description)
        if condition():
            return
        executor.spin_once(timeout_sec=0.05)
    pytest.fail(f"Timeout waiting for {description}:\n{engine.output()}")


@pytest.fixture
def ros_node():
    context = Context()
    rclpy.init(context=context)
    node = rclpy.create_node("bpe_integration_regression", context=context)
    executor = SingleThreadedExecutor(context=context)
    executor.add_node(node)
    try:
        yield node, executor
    finally:
        executor.remove_node(node)
        executor.shutdown()
        node.destroy_node()
        rclpy.shutdown(context=context)


def yaml_path(name):
    return str(Path(os.environ["BPE_TEST_CONFIG_DIR"]) / f"test_03_{name}.yaml")


def await_state(node, executor, engine, expected):
    client = node.create_client(GetState, "/block_plugin_engine/get_state")
    try:
        spin_until(executor, engine, client.service_is_ready, "get_state service")
        state = None

        def query():
            nonlocal state
            future = client.call_async(GetState.Request())
            spin_until(executor, engine, future.done, "get_state response")
            assert future.result() is not None, engine.output()
            state = future.result().current_state.id

        deadline = time.monotonic() + STARTUP_TIMEOUT
        while time.monotonic() < deadline:
            query()
            if state == expected:
                return
            time.sleep(0.05)
        pytest.fail(f"Expected lifecycle state {expected}, found {state}:\n{engine.output()}")
    finally:
        node.destroy_client(client)


@pytest.mark.parametrize("scenario", [
    "00_empty", "10_internal_transform", "20_ros_input", "30_ros_output",
])
def test_configure_inactive_shutdown(ros_node, scenario):
    node, executor = ros_node
    engine = EngineProcess(yaml_path(scenario), autostart=False)
    try:
        change = node.create_client(ChangeState, "/block_plugin_engine/change_state")
        try:
            spin_until(executor, engine, change.service_is_ready, "change_state service")
            request = ChangeState.Request()
            request.transition.id = Transition.TRANSITION_CONFIGURE
            future = change.call_async(request)
            spin_until(executor, engine, future.done, "configure transition response")
            assert future.result() is not None and future.result().success, engine.output()
        finally:
            node.destroy_client(change)
        await_state(node, executor, engine, State.PRIMARY_STATE_INACTIVE)
        engine.stop()
        engine = None
    finally:
        if engine is not None:
            engine.stop(require_clean=False)


def test_full_twist_stamped_pipeline(ros_node):
    node, executor = ros_node
    outputs = []
    publisher = node.create_publisher(TwistStamped, "/bpe_test/input", 10)
    subscriber = node.create_subscription(
        TwistStamped, "/bpe_test/output", outputs.append, 10)
    engine = EngineProcess(yaml_path("90_full_pipeline"), autostart=True)
    try:
        engine.await_active()
        await_state(node, executor, engine, State.PRIMARY_STATE_ACTIVE)
        spin_until(
            executor, engine,
            lambda: publisher.get_subscription_count() > 0
            and subscriber.get_publisher_count() > 0,
            "ROS boundary discovery",
        )
        msg = TwistStamped()
        msg.twist.linear.x = 1.5
        msg.twist.linear.y = -2.0
        msg.twist.linear.z = 3.0
        msg.twist.angular.x = -1.0
        msg.twist.angular.y = 2.0
        msg.twist.angular.z = 0.5

        deadline = time.monotonic() + MESSAGE_TIMEOUT
        while not outputs and time.monotonic() < deadline:
            publisher.publish(msg)
            executor.spin_once(timeout_sec=0.05)
            engine.check_alive("receiving TwistStamped")
        assert outputs, f"No output TwistStamped:\n{engine.output()}"
        result = outputs[-1].twist
        assert result.linear.x == 3.0
        assert result.linear.y == 2.0
        assert result.linear.z == 3.0
        assert result.angular.x == -1.0
        assert result.angular.y == 2.0
        assert result.angular.z == 0.75
        engine.stop()
        engine = None
    finally:
        if engine is not None:
            engine.stop(require_clean=False)
        node.destroy_publisher(publisher)
        node.destroy_subscription(subscriber)
