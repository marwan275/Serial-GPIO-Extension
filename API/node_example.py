#!/usr/bin/env python3
"""
multi_client_example.py
Four SGPIO clients in the same node:
  - Client 1 blinks a digital output on pin 13 (period ~1s)
  - Client 2 reads a digital input from pin 12 every 1 second
  - Client 3 reads an analog input from pin 34 every 2 seconds
  - Client 4 sweeps a servo on pin 9 between 0° and 180° every 2 seconds
"""

import rclpy
from rclpy.node import Node
from .sgpio_lib import SGPIO


class MultiClientNode(Node):
    def __init__(self):
        super().__init__("multi_client_node")

        # Create four independent GPIO clients
        self.gpio_led = SGPIO(self, client_id=1)  # digital write (pin 13)
        self.gpio_sensor = SGPIO(self, client_id=2)  # digital read (pin 12)
        self.gpio_analog = SGPIO(self, client_id=3)  # analog read (pin 34)
        self.gpio_servo = SGPIO(self, client_id=4)  # servo write (pin 9)

        # Blink the LED with a simple periodic toggle.
        self.led_state = False
        self.led_write_in_flight = False
        self.led_timer = self.create_timer(0.5, self.trigger_led_blink)

        # Periodic sensor reading every 1 second
        self.read_timer = self.create_timer(1.0, self.trigger_digital_read)

        # Periodic analog reading every 2 seconds
        self.analog_timer = self.create_timer(2.0, self.trigger_analog_read)

        # Servo sweeping: start at 0°, then toggle every 2 seconds
        self.servo_angle = 0
        self.servo_timer = self.create_timer(1.0, self.trigger_servo_sweep)

        self.get_logger().info("Multi-client node started (4 clients)")

    # ──────────────────── LED BLINK (client 1) ────────────────────
    def trigger_led_blink(self):
        if self.led_write_in_flight:
            return

        next_state = not self.led_state
        self.led_write_in_flight = True
        self.gpio_led.digitalWrite(
            13,
            next_state,
            done_callback=lambda result: self.on_led_write(result, next_state),
        )

    def on_led_write(self, result, next_state):
        self.led_write_in_flight = False
        if result and result.success:
            self.led_state = next_state
            state_label = "ON" if self.led_state else "OFF"
            self.get_logger().info(f"LED {state_label}")
        else:
            self.get_logger().error("LED write failed")

    # ──────────────────── DIGITAL READ (client 2) ────────────────
    def trigger_digital_read(self):
        self.gpio_sensor.digitalRead(12, done_callback=self.on_digital_read)

    def on_digital_read(self, result):
        if result and result.success:
            value = bool(result.result_value)
            self.get_logger().info(f"Digital pin 12 = {value}")
        else:
            self.get_logger().error("Digital read failed")

    # ──────────────────── ANALOG READ (client 3) ─────────────────
    def trigger_analog_read(self):
        self.gpio_analog.analogRead(34, done_callback=self.on_analog_read)

    def on_analog_read(self, result):
        if result and result.success:
            self.get_logger().info(f"Analog pin 34 = {result.result_value}")
        else:
            self.get_logger().error("Analog read failed")

    # ──────────────────── SERVO SWEEP (client 4) ─────────────────
    def trigger_servo_sweep(self):
        # Toggle between 0° and 180° every 2 seconds
        angle = 0 if self.servo_angle == 180 else 180
        self.servo_angle = angle
        self.gpio_servo.servoWrite(9, angle, done_callback=self.on_servo_write)

    def on_servo_write(self, result):
        if result and result.success:
            self.get_logger().info(f"Servo pin 9 set to {self.servo_angle}°")
        else:
            self.get_logger().error("Servo write failed")


def main():
    rclpy.init()
    node = MultiClientNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
