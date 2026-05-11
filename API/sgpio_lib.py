from rclpy.action import ActionClient
from rclpy.node import Node
from interfaces.action import GpioFrame
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

    def _fail_request(self, message: str, done_callback, exc: Exception | None = None):
        if exc is None:
            self.logger.error(message)
        else:
            self.logger.error(f"{message}: {exc}")

        try:
            done_callback(None)
        except Exception as callback_exc:
            self.logger.error(
                f"SGPIO: failure callback raised while handling error: {callback_exc}"
            )

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
            f"SGPIO: sending goal {goal.request_id} (pin {goal.pin_number}), value={goal.value}, type={frame.type.name}, pin_type={frame.pin_type.name}, priority={frame.priority.name}"
        )

        # 1) Send the goal
        try:
            send_future = self._action_client.send_goal_async(goal)
        except Exception as exc:
            self._fail_request("SGPIO: failed to send goal", done_callback, exc)
            return

        # 2) When the goal is accepted, fetch the result
        send_future.add_done_callback(
            lambda future: self._on_goal_response(future, done_callback)
        )

    def _on_goal_response(self, send_future, done_callback):
        try:
            goal_handle = send_future.result()
        except Exception as exc:
            self._fail_request("SGPIO: goal response failed", done_callback, exc)
            return

        if goal_handle is None or not goal_handle.accepted:
            self._fail_request("SGPIO: goal rejected", done_callback)
            return

        try:
            result_future = goal_handle.get_result_async()
        except Exception as exc:
            self._fail_request(
                "SGPIO: failed to request action result", done_callback, exc
            )
            return

        result_future.add_done_callback(
            lambda future: self._on_result(future, done_callback)
        )

    def _on_result(self, result_future, done_callback):
        try:
            response = result_future.result()  # action result wrapper
        except Exception as exc:
            self._fail_request("SGPIO: result retrieval failed", done_callback, exc)
            return

        result = getattr(response, "result", None)
        if result is None:
            self._fail_request("SGPIO: action result was empty", done_callback)
            return

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

    def dacWrite(self, address: int, value: int, done_callback, priority: bool = False):
        self.mcp4725Write(address, value, done_callback, priority)

    def destroy(self) -> None:
        try:
            self._action_client.destroy()
        except Exception:
            pass
