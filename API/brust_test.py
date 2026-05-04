#!/usr/bin/env python3
"""
burst_test.py – sends a burst of digitalRead requests and prints latency stats.
Usage: python3 burst_test.py [burst_size]   (default 200)
"""

import sys
import time
import rclpy
from rclpy.node import Node
from serial_gpio.sgpio_lib import SGPIO


class BurstTestNode(Node):
    def __init__(self, burst_size=200):
        super().__init__("burst_test")
        self.gpio = SGPIO(self, client_id=99)
        self.burst_size = burst_size
        self.latencies = []
        self.completed = 0
        self.send_start = 0.0  # will be set in run()

    # per‑request callback factory – captures send time
    def _make_callback(self):
        send_time = time.time()

        def callback(result):
            elapsed = time.time() - send_time
            self.latencies.append(elapsed)
            self.completed += 1
            if self.completed == self.burst_size:
                self._print_stats()

        return callback

    def run(self):
        self.get_logger().info(f"Sending {self.burst_size} read requests in burst...")
        self.send_start = time.time()
        for _ in range(self.burst_size):
            self.gpio.digitalRead(12, done_callback=self._make_callback())

    def _print_stats(self):
        total_time = time.time() - self.send_start
        lat = sorted(self.latencies)
        self.get_logger().info(
            f"All {self.burst_size} requests completed in {total_time:.3f}s"
        )
        self.get_logger().info(
            f"Latency (ms) – min: {lat[0] * 1000:.1f}, "
            f"max: {lat[-1] * 1000:.1f}, "
            f"avg: {sum(lat) / len(lat) * 1000:.1f}, "
            f"p50: {lat[len(lat) // 2] * 1000:.1f}, "
            f"p99: {lat[int(len(lat) * 0.99)] * 1000:.1f}"
        )
        self.get_logger().info(f"Throughput: {self.burst_size / total_time:.1f} req/s")


def main():
    rclpy.init()
    # Accept burst size from command line, e.g. python3 burst_test.py 500
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    node = BurstTestNode(burst_size=size)
    node.run()
    rclpy.spin(node)  # wait for all callbacks
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
