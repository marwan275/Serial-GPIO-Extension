from typing import Union
from enum import Enum
from dataclasses import dataclass


class FrameType(Enum):
    NONE = "N"
    READ = "R"
    WRITE = "W"
    RESPONSE = "S"
    ERROR = "E"
    DEBUG = "B"


class PinType(Enum):
    NONE = "N"
    DIGITAL = "D"
    ANALOG = "A"
    SERVO = "S"
    MCP4725 = "G"


class FramePriority(Enum):
    NONE = "N"
    LOW = "L"
    HIGH = "H"


class ErrorCode(Enum):
    BAD_FRAME = 0
    BAD_PIN = 1
    BAD_FUNCTION = 2
    BAD_PIN_TYPE = 3
    BAD_VALUE = 4
    MISSING_SESSION = 5
    UNSUPPORTED = 6
    QUEUE_FULL = 7
    CANCELLED = 8
    NONE = 255


@dataclass
class RequestFrame:
    #     uint16_t request_id;
    #     uint16_t value;
    #     FrameType type; // kRead or kWrite
    #     Priority priority;
    #     PinType pin_type;
    #     uint8_t pin_number;
    request_id: int = 0
    value: int = 0
    type: FrameType = FrameType.NONE
    priority: FramePriority = FramePriority.NONE
    pin_type: PinType = PinType.NONE
    pin_number: int = 0


@dataclass
class ResponseFrame:
    # uint16_t request_id;
    # uint16_t value;
    # FrameType type;  // kResponse or kError
    # ErrorCode error; // valid only when type == kError
    request_id: int = 0
    value: int = 0
    type: FrameType = FrameType.NONE
    error: ErrorCode = ErrorCode.NONE


Frame = Union[RequestFrame, ResponseFrame]


# Map string enum values to uint8 for ROS action communication
FRAME_TYPE_TO_INT = {
    FrameType.NONE: 0,
    FrameType.READ: 1,
    FrameType.WRITE: 2,
    FrameType.RESPONSE: 3,
    FrameType.ERROR: 4,
    FrameType.DEBUG: 5,
}

PIN_TYPE_TO_INT = {
    PinType.NONE: 0,
    PinType.DIGITAL: 1,
    PinType.ANALOG: 2,
    PinType.SERVO: 3,
    PinType.MCP4725: 4,
}

PRIORITY_TO_INT = {
    FramePriority.NONE: 0,
    FramePriority.LOW: 1,
    FramePriority.HIGH: 2,
}

# Reverse maps (optional, for the server side)
INT_TO_FRAME_TYPE = {v: k for k, v in FRAME_TYPE_TO_INT.items()}
INT_TO_PIN_TYPE = {v: k for k, v in PIN_TYPE_TO_INT.items()}
INT_TO_PRIORITY = {v: k for k, v in PRIORITY_TO_INT.items()}
