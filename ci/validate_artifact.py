#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause

"""Exercise the installed examples as an external consumer of BPE."""

import math
import os
import signal
import subprocess
import threading
import time
from contextlib import contextmanager

import rclpy
from geometry_msgs.msg import PoseStamped, Twist
from std_msgs.msg import Int32


class Example:
    def __init__(self, name):
        self.lines = []
        self.process = subprocess.Popen(
            ["ros2", "launch", "bpe_examples", f"{name}.launch.yaml"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            start_new_session=True,
            env={**os.environ, "RCUTILS_LOGGING_BUFFERED_STREAM": "0", "PYTHONUNBUFFERED": "1"},
        )
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        with self.process.stdout as stream:
            for line in stream:
                self.lines.append(line)
                print(line, end="", flush=True)

    def seen(self, text):
        return any(text in line for line in self.lines)

    def wait(self, predicate, description, node=None, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"Launcher exited while waiting for {description}")
            if predicate():
                return
            if node is None:
                time.sleep(0.05)
            else:
                rclpy.spin_once(node, timeout_sec=0.05)
        raise RuntimeError(f"Timeout waiting for {description}")

    def stop(self, clean):
        premature = self.process.poll() is not None
        if not premature:
            self.process.send_signal(signal.SIGINT)
        try:
            code = self.process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait()
            raise RuntimeError("Launcher failed to stop")
        finally:
            # Also clean up any orphaned children after an abnormal launch exit.
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.reader.join(timeout=5)
        if clean and (premature or code != 0 or not self.seen("BlockPluginEngine shutdown completed")
                      or self.seen("process has died")):
            raise RuntimeError(f"Unclean example shutdown (launcher status {code})")


@contextmanager
def launch(name):
    print(f"=== {name} ===", flush=True)
    example = Example(name)
    clean = False
    try:
        example.wait(lambda: example.seen("BlockPluginEngine active"), "BPE activation")
        yield example
        clean = True
    finally:
        example.stop(clean)


def close(a, b):
    return math.isclose(a, b, rel_tol=1e-7, abs_tol=1e-7)


def discovered(example, node, pubs, subs):
    example.wait(lambda: all(p.get_subscription_count() > 0 for p in pubs)
                 and all(s.get_publisher_count() > 0 for s in subs),
                 "DDS discovery", node)


def boundary(node):
    received = []
    pub = node.create_publisher(PoseStamped, "/bpe_examples/pose", 10)
    sub = node.create_subscription(Twist, "/bpe_examples/velocity", received.append, 10)
    try:
        with launch("03_ros_boundary") as example:
            discovered(example, node, [pub], [sub])
            for x, y, vx, vy in [(3.0, -1.0, 1.0, -0.5), (-3.0, 1.0, -1.0, 0.5)]:
                received.clear()
                msg = PoseStamped()
                msg.header.frame_id = "map"
                msg.pose.position.x = x
                msg.pose.position.y = y
                msg.pose.orientation.w = 1.0
                pub.publish(msg)
                example.wait(lambda: any(close(m.linear.x, vx) and close(m.linear.y, vy)
                                         for m in received), "transformed velocity", node)
    finally:
        node.destroy_subscription(sub)
        node.destroy_publisher(pub)


def hybrid(node):
    commands, alarms = [], []
    speed = node.create_publisher(Int32, "/bpe_examples/speed", 10)
    direction = node.create_publisher(Twist, "/bpe_examples/direction", 10)
    command = node.create_subscription(Twist, "/bpe_examples/command", commands.append, 10)
    alarm = node.create_subscription(Int32, "/bpe_examples/alarm", alarms.append, 10)
    try:
        with launch("04_hybrid") as example:
            discovered(example, node, [speed, direction], [command, alarm])
            for value, z, expected_alarm in [(8, 0.3, 1), (3, -0.2, 0)]:
                commands.clear()
                alarms.clear()
                msg = Twist()
                msg.angular.z = z
                speed.publish(Int32(data=value))
                direction.publish(msg)
                example.wait(lambda: any(close(m.linear.x, value * 0.1)
                                         and close(m.angular.z, z) for m in commands)
                             and any(m.data == expected_alarm for m in alarms),
                             "hybrid command and alarm", node)
                example.wait(lambda: example.seen(f"[monitor] linear.x={value * 0.1:.1f}"),
                             "periodic monitor", node)
    finally:
        node.destroy_subscription(command)
        node.destroy_subscription(alarm)
        node.destroy_publisher(speed)
        node.destroy_publisher(direction)


def main():
    with launch("01_basic_pipeline") as example:
        example.wait(lambda: example.seen("[Report]"), "report")
    with launch("02_multichannel") as example:
        example.wait(lambda: example.seen("[Report]") and example.seen("[Summary]"),
                     "report and summary")
    rclpy.init()
    node = rclpy.create_node("bpe_artifact_consumer")
    try:
        boundary(node)
        hybrid(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
    print("All installed BPE examples passed", flush=True)


if __name__ == "__main__":
    main()
