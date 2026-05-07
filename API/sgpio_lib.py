from rclpy.action import ActionClient
from rclpy.node import Node
from serial_gpio_interfaces.action import GpioFrame
from .frame_codec import (
    RequestFrame,
    FrameType,
    PinType,
    FramePriority,
    FRAME_TYPE_TO_INT,
    PIN_TYPE_TO_INT,
    PRIORITY_TO_INT,
)


class SGPIO:
    """
    Non-blocking GPIO client. Every operation takes a `done_callback` that is
    called when the Teensy finally responds.
    """

    def __init__(self, node: Node, client_id: int, action_name="/sgpio/frame"):
        self.node = node
        self.client_id = client_id
        self.action_name = action_name
        self._req_id = 0
        self._action_client = ActionClient(node, GpioFrame, action_name)
        self.logger = node.get_logger()

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    def _send_goal(self, frame: RequestFrame, done_callback):
        """
        Sends a goal and wires up the response chain.
        done_callback(result: GpioFrame.Result) will be called on success,
        or done_callback(None) if the goal is rejected or fails.
        """
        if not self._action_client.server_is_ready():
            self.logger.info(f"SGPIO: waiting for action server {self.action_name}")
            if not self._action_client.wait_for_server(timeout_sec=2.0):
                self.logger.error(
                    f"SGPIO: action server {self.action_name} not available"
                )
                done_callback(None)
                return

        goal = GpioFrame.Goal()
        goal.client_id = self.client_id
        goal.request_id = frame.request_id
        goal.type = FRAME_TYPE_TO_INT[frame.type]
        goal.pin_type = PIN_TYPE_TO_INT[frame.pin_type]
        goal.priority = PRIORITY_TO_INT[frame.priority]
        goal.pin_number = frame.pin_number
        goal.value = frame.value

        self.logger.info(
            f"SGPIO: sending goal {goal.request_id} (pin {goal.pin_number})"
        )

        # 1) Send the goal
        send_future = self._action_client.send_goal_async(goal)
        # 2) When the goal is accepted, fetch the result
        send_future.add_done_callback(
            lambda future: self._on_goal_response(future, done_callback)
        )

    def _on_goal_response(self, send_future, done_callback):
        goal_handle = send_future.result()
        if not goal_handle.accepted:
            self.logger.error("SGPIO: goal rejected")
            done_callback(None)  # signal failure
            return

        result_future = goal_handle.get_result_async()
        result_future.add_done_callback(
            lambda future: self._on_result(future, done_callback)
        )

    def _on_result(self, result_future, done_callback):
        response = result_future.result()  # action result wrapper
        result = response.result  # the actual GpioFrame.Result
        done_callback(result)

    def digitalWrite(
        self, pin: int, value: bool, done_callback, priority: bool = False
    ):
        """
        Write a digital value.
        done_callback(result) will receive a GpioFrame.Result object.
        """
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.WRITE,
            pin_type=PinType.DIGITAL,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=pin,
            value=int(value),
        )
        self._send_goal(frame, done_callback)

    def digitalRead(self, pin: int, done_callback, priority: bool = False):
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.READ,
            pin_type=PinType.DIGITAL,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=pin,
            value=0,
        )
        self._send_goal(frame, done_callback)

    def analogWrite(self, pin: int, value: int, done_callback, priority: bool = False):
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.WRITE,
            pin_type=PinType.ANALOG,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=pin,
            value=value,
        )
        self._send_goal(frame, done_callback)

    def analogRead(self, pin: int, done_callback, priority: bool = False):
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.READ,
            pin_type=PinType.ANALOG,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=pin,
            value=0,
        )
        self._send_goal(frame, done_callback)

    def servoWrite(self, pin: int, angle: int, done_callback, priority: bool = False):
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.WRITE,
            pin_type=PinType.SERVO,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=pin,
            value=angle,
        )
        self._send_goal(frame, done_callback)

    def mcp4725Write(
        self, address: int, value: int, done_callback, priority: bool = False
    ):
        frame = RequestFrame(
            request_id=self._next_id(),
            type=FrameType.WRITE,
            pin_type=PinType.MCP4725,
            priority=FramePriority.LOW if not priority else FramePriority.HIGH,
            pin_number=address,
            value=value,
        )
        self._send_goal(frame, done_callback)
