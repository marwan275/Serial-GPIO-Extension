#!/usr/bin/env python3
"""
burst_test.py - sends a burst of digitalRead requests and prints latency stats.
Usage: python -m serial_gpio.brust_test [burst_size]   (default 200)
"""

import os
import sys
import time

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node

if __package__ in (None, ""):
    package_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if package_root not in sys.path:
        sys.path.insert(0, package_root)
    from serial_gpio.sgpio_lib import SGPIO
else:
    from .sgpio_lib import SGPIO


class BurstTestNode(Node):
    def __init__(self, burst_size=200):
        super().__init__("burst_test")
        self.gpio = SGPIO(self, client_id=99)
        self.burst_size = burst_size
        self.latencies = []
        self.completed = 0
        self.finished = False
        self.send_start = 0.0  # will be set in run()
        self.timeout_sec = max(10.0, min(60.0, self.burst_size * 0.5))
        self.start_timer = self.create_timer(0.1, self._start_once)
        self.timeout_timer = None

    # per‑request callback factory – captures send time
    def _make_callback(self):
        send_time = time.time()

        def callback(result):
            if self.finished:
                return
            elapsed = time.time() - send_time
            self.latencies.append(elapsed)
            self.completed += 1
            if self.completed == self.burst_size:
                self._print_stats()

        return callback

    def run(self):
        self.get_logger().info(f"Sending {self.burst_size} read requests in burst...")
        self.send_start = time.time()
        self.timeout_timer = self.create_timer(self.timeout_sec, self._on_timeout)
        for _ in range(self.burst_size):
            self.gpio.digitalRead(12, done_callback=self._make_callback())

    def _start_once(self):
        self.start_timer.cancel()
        self.run()

    def _on_timeout(self):
        if self.finished:
            return

        pending = self.burst_size - self.completed
        self.get_logger().error(
            f"Burst timed out after {self.timeout_sec:.1f}s with {pending} pending requests"
        )
        self._print_stats()

    def _print_stats(self):
        if self.finished:
            return

        self.finished = True
        if self.timeout_timer is not None:
            self.timeout_timer.cancel()

        total_time = time.time() - self.send_start
        lat = sorted(self.latencies)
        self.get_logger().info(
            f"Completed {self.completed}/{self.burst_size} requests in {total_time:.3f}s"
        )
        if not lat:
            self.get_logger().error("No requests completed before shutdown")
            if rclpy.ok():
                rclpy.shutdown()
            return

        self.get_logger().info(f"All completed requests finished in {total_time:.3f}s")
        self.get_logger().info(
            f"Latency (ms) – min: {lat[0] * 1000:.1f}, "
            f"max: {lat[-1] * 1000:.1f}, "
            f"avg: {sum(lat) / len(lat) * 1000:.1f}, "
            f"p50: {lat[len(lat) // 2] * 1000:.1f}, "
            f"p99: {lat[int(len(lat) * 0.99)] * 1000:.1f}"
        )
        self.get_logger().info(f"Throughput: {self.completed / total_time:.1f} req/s")
        if rclpy.ok():
            rclpy.shutdown()


def main():
    rclpy.init()
    # Accept burst size from command line, e.g. python3 burst_test.py 500
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    node = BurstTestNode(burst_size=size)
    try:
        rclpy.spin(node)  # wait for all callbacks
    except ExternalShutdownException:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
