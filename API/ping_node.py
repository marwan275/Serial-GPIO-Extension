import os
import sys
import time

import rclpy
from rclpy.node import Node

if __package__ in (None, ""):
    package_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if package_root not in sys.path:
        sys.path.insert(0, package_root)
    from serial_gpio.sgpio_lib import SGPIO
else:
    from .sgpio_lib import SGPIO


class LatencyTestNode(Node):
    def __init__(self):
        super().__init__("latency_test")
        self.gpio = SGPIO(self, client_id=1)
        self.test_pin = 12
        self.request_in_flight = False
        self.read_timer = self.create_timer(1.0, self._maybe_run_test)
        self.run_test()

    def _maybe_run_test(self):
        if self.request_in_flight:
            return

        self.run_test()

    def run_test(self):
        self.request_in_flight = True
        self.start_time = time.time()
        self.gpio.digitalRead(self.test_pin, done_callback=self.on_done)

    def on_done(self, result):
        elapsed = time.time() - self.start_time
        self.request_in_flight = False
        self.get_logger().info(f"Round-trip latency: {elapsed * 1000:.1f} ms")


def main():
    rclpy.init()
    node = LatencyTestNode()
    rclpy.spin(node)


if __name__ == "__main__":
    main()
