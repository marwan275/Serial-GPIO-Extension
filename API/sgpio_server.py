# serial_gpio/sgpio_server.py
import rclpy
from rclpy.node import Node
from rclpy.action import ActionServer
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
import threading
import queue

from serial_gpio.teensy_serial_handler import TeensySerialHandler
from serial_gpio.sgpio_frame_sender import SGPIOFrameSender
from serial_gpio.sgpio_frame_receiver import SGPIOFrameReceiver
from serial_gpio.frame_codec import (
    RequestFrame,
    ResponseFrame,
    INT_TO_FRAME_TYPE,
    INT_TO_PIN_TYPE,
    INT_TO_PRIORITY,
    ErrorCode,
    FrameType,
    PinType,
)

# The action definition
from serial_gpio_interfaces.action import GpioFrame


class GpioServer(Node):
    def __init__(self):
        super().__init__("gpio_server")
        self._action_server = ActionServer(
            self,
            GpioFrame,
            "sgpio/frame",
            execute_callback=self.execute_callback,
            callback_group=ReentrantCallbackGroup(),
        )
        self.stop_token = threading.Event()
        self.sender_frames_queue = queue.Queue()
        self.receiver_frames_queue = queue.Queue()
        self.serial_handler = TeensySerialHandler(logger=self.get_logger())
        if not self.serial_handler.connect():
            self.get_logger().error("Failed to connect to Teensy")
            raise RuntimeError("Failed to connect to Teensy")

        self._next_serial_id = 0
        self.serial_id_lock = threading.Lock()
        self.serial_id_map_lock = threading.Lock()
        self.serial_id_map = {}  # serial_id -> (client_id, request_id, goal_handle)

        # 1) Create sender / receiver
        self.sender = SGPIOFrameSender(
            serial_handler=self.serial_handler,
            frames_queue=self.sender_frames_queue,
            stop_token=self.stop_token,
            logger=self.get_logger(),
        )
        self.sender.start()

        self.receiver_thread = threading.Thread(
            target=self._receiver_loop, daemon=True, name="receiver_loop"
        )
        self.receiver_thread.start()

        self.receiver = SGPIOFrameReceiver(
            serial_handler=self.serial_handler,
            frames_queue=self.receiver_frames_queue,
            stop_token=self.stop_token,
            logger=self.get_logger(),
        )
        self.receiver.start()

        self.get_logger().info("SGPIO Server Ready")

    # ---------- Action callback ----------
    def execute_callback(self, goal_handle):
        request = goal_handle.request
        self.get_logger().info(
            f"New goal: pin={request.pin_number}, type={request.type}"
        )

        # Convert the action request back to a serial frame
        frame_type = INT_TO_FRAME_TYPE[request.type]
        pin_type = INT_TO_PIN_TYPE[request.pin_type]
        priority = INT_TO_PRIORITY[request.priority]

        # Allocate a unique serial ID only for READs (writes don't need a response)
        if frame_type == FrameType.READ:
            sid = self._allocate_serial_id()
        else:
            sid = None

        # # Build the request frame
        request_frame = RequestFrame(
            request_id=sid if sid is not None else 0,
            type=frame_type,
            pin_type=pin_type,
            priority=priority,
            pin_number=request.pin_number,
            value=request.value,
        )
        if frame_type == FrameType.READ:
            completion_event = threading.Event()
            with self.serial_id_map_lock:
                self.serial_id_map[sid] = (
                    request.request_id,
                    request.client_id,
                    goal_handle,
                    completion_event,
                )

        # Attempt to enqueue the frame
        try:
            self.sender_frames_queue.put_nowait(request_frame)
        except queue.Full:
            self.get_logger().error("Sender queue full - aborting goal")
            if frame_type == FrameType.READ:
                with self.serial_id_map_lock:
                    self.serial_id_map.pop(sid, None)
            goal_handle.abort()
            result = GpioFrame.Result()
            result.success = False
            result.error_code = ErrorCode.QUEUE_FULL
            result.message = "Sender queue full"
            return result

        # ── WRITE / SERVO: succeed immediately ──
        if frame_type != FrameType.READ:
            result = GpioFrame.Result()
            result.success = True
            result.result_value = 0
            result.error_code = 0
            result.message = "OK (write)"
            goal_handle.succeed()
            return result

        # ── READ: wait for serial response ──
        if not completion_event.wait(timeout=2.0):  # 2 seconds timeout
            self.get_logger().error(f"Timeout waiting for read response (sid={sid})")
            with self.serial_id_map_lock:
                self.serial_id_map.pop(sid, None)
            goal_handle.abort()
            result = GpioFrame.Result()
            result.success = False
            result.error_code = 255
            result.message = "Timeout"
            return result

        # Retrieve the response stored by the receiver thread
        response = getattr(goal_handle, "_serial_response", None)
        with self.serial_id_map_lock:
            self.serial_id_map.pop(sid, None)

        if response is None:
            self.get_logger().error("No response attached")
            goal_handle.abort()
            return GpioFrame.Result()

        result = GpioFrame.Result()
        result.success = response.error == ErrorCode.NONE
        result.result_value = response.value
        result.error_code = response.error.value
        result.message = "OK" if result.success else f"Error {response.error.value}"

        if result.success:
            goal_handle.succeed()
        else:
            goal_handle.abort()
        return result

    # ---------- Process receiver queue ----------
    def _receiver_loop(self):
        while not self.stop_token.is_set():
            try:
                response = self.receiver_frames_queue.get(timeout=0.5)
            except queue.Empty:
                continue

            if response is None:
                continue
            if not isinstance(response, ResponseFrame):
                self.get_logger().error(
                    f"Received unsupported frame type: {type(response)}"
                )
                continue

            serial_id = response.request_id
            with self.serial_id_map_lock:
                mapping = self.serial_id_map.get(serial_id)  # ← use get, not pop
            if mapping is None:
                self.get_logger().warning(f"Unsolicited response for {serial_id}")
                continue

            # Unpack the 4-element tuple
            _, _, goal_handle, event = mapping

            # Store the response on the goal handle so the waiting thread can pick it up
            goal_handle._serial_response = response

            # Wake up the waiting execute_callback
            event.set()

    def _allocate_serial_id(self) -> int:
        """Return the next unique serial ID (0-65535)."""
        with self.serial_id_lock:
            sid = self._next_serial_id
            self._next_serial_id = (self._next_serial_id + 1) % 65536
        return sid


def main():
    rclpy.init()
    node = GpioServer()

    executor = MultiThreadedExecutor()
    executor.add_node(node)

    try:
        print("Entering spin...")
        executor.spin()
        print("Spin returned.")  # if we ever see this, something is wrong
    except KeyboardInterrupt:
        print("Keyboard interrupt")
    except Exception as e:
        print(f"Exception in spin: {e}")
        import traceback

        traceback.print_exc()
    finally:
        node.destroy_node()
        rclpy.shutdown()
        print("Shutdown complete.")

    # Keep the process alive for inspection
    import time

    time.sleep(5)  # 5 seconds to check if server is alive
    print("Exiting main.")


if __name__ == "__main__":
    main()
