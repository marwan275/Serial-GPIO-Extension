from serial_gpio.frame_codec import (
    RequestFrame,
    FrameType,
    PinType,
    FramePriority,
    ErrorCode,
    ResponseFrame,
    Frame,
)


class SGPIOFrameHandler:
    def __init__(self, logger):
        self.logger = logger

    def frameFromString(self, frame_str: str) -> Frame:
        try:
            if not frame_str.startswith("@") or not frame_str.endswith(";"):
                raise ValueError("Invalid frame format")

            content = frame_str[1:-1]
            parts = content.split(",")

            # Request frame
            # @<type>,<requestId>,<pinType>,<priority>,<pin>,<value>;
            # @W,2,D,H,13,11;
            if len(parts) == 6:
                (
                    type_char,
                    request_id_str,
                    pin_type_char,
                    priority_str,
                    pin_number_str,
                    value_str,
                ) = parts

                return RequestFrame(
                    request_id=int(request_id_str),
                    value=int(value_str),
                    type=FrameType(type_char),
                    pin_type=PinType(pin_type_char),
                    priority=FramePriority(priority_str),
                    pin_number=int(pin_number_str),
                )

            # Response frame
            # @<type>,<requestId>,<value>,<error>;\n
            # @S,2,0,255;
            if len(parts) == 4:
                (
                    type_char,
                    request_id_str,
                    value_str,
                    error_code_str,
                ) = parts

                return ResponseFrame(
                    request_id=int(request_id_str),
                    value=int(value_str),
                    type=FrameType(type_char),
                    error=ErrorCode(int(error_code_str)),
                )

            raise ValueError(f"Unexpected frame size: {len(parts)}")

        except Exception as e:
            self.logger.error(f"Failed to parse frame '{frame_str}': {e}")
            raise ValueError(f"Failed to parse frame: {e}") from e

    def stringFromFrame(self, frame: Frame) -> str:
        try:
            if isinstance(frame, RequestFrame):
                # @W,2,D,H,13,1;
                return (
                    f"@{frame.type.value},"
                    f"{frame.request_id},"
                    f"{frame.pin_type.value},"
                    f"{frame.priority.value},"
                    f"{frame.pin_number},"
                    f"{frame.value};"
                )

            if isinstance(frame, ResponseFrame):
                # @S,2,0,255;
                return (
                    f"@{frame.type.value},"
                    f"{frame.request_id},"
                    f"{frame.value},"
                    f"{frame.error.value};"
                )

            raise TypeError(f"Unsupported frame type: {type(frame)}")

        except Exception as e:
            self.logger.error(f"Failed to encode frame: {e}")
            raise ValueError(f"Failed to encode frame: {e}") from e
