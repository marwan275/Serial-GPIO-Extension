import time
import rclpy
from rclpy.node import Node
from serial_gpio.sgpio_lib import SGPIO


class LatencyTestNode(Node):
    def __init__(self):
        super().__init__("latency_test")
        self.gpio = SGPIO(self, client_id=1)
        self.test_pin = 12
        self.run_test()

    def run_test(self):
        self.start_time = time.time()
        self.gpio.digitalRead(self.test_pin, done_callback=self.on_done)

    def on_done(self, result):
        elapsed = time.time() - self.start_time
        self.get_logger().info(f"Round‑trip latency: {elapsed * 1000:.1f} ms")
        # run again after 1 second
        self.create_timer(1.0, self.run_test)


def main():
    rclpy.init()
    node = LatencyTestNode()
    rclpy.spin(node)
    
if __name__ == "__main__":
    main()
