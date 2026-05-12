#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

// Protocol enums keep their ASCII wire token as the underlying value so the
// codec helpers can translate between bytes and typed fields cheaply.
enum class FrameType : uint8_t
{
    kNone = 'N',
    kRead = 'R',
    kWrite = 'W',
    kResponse = 'S',
    kError = 'E',
    kDebug = 'B'
};

enum class PinType : uint8_t
{
    kNone = 'N',
    kDigital = 'D',
    kAnalog = 'A',
    kServo = 'S',
    kMCP4725 = 'G',
};

enum class PinMode : uint8_t
{
    kNone = 0,
    kInput,
    kOutput,
};

enum class Priority : uint8_t
{
    kNone = 'N',
    kLow = 'L',
    kHigh = 'H'
};

enum class ErrorCode : uint8_t
{
    kBadFrame = 0,
    kBadPin = 1,
    kBadFunction = 2,
    kBadPinType = 3,
    kBadValue = 4,
    kMissingSession = 5,
    kUnsupported = 6,
    kQueueFull = 7,
    kCancelled = 8,
    kNone = 255,
};

namespace FrameTypeCodec
{
    // Request frames only accept Read and Write at the parser boundary.
    inline FrameType frameTypeFromChar(char value)
    {
        switch (value)
        {
        case 'R':
            return FrameType::kRead;
        case 'W':
            return FrameType::kWrite;
        case 'B':
            return FrameType::kDebug;
        default:
            return FrameType::kNone;
        }
    }

    inline char frameTypeToChar(FrameType frameType)
    {
        return static_cast<char>(frameType);
    }
} // namespace FrameTypeCodec

namespace PinTypeCodec
{
    // Pin type codes describe the hardware mode the request targets.
    inline PinType pinTypeFromChar(char value)
    {
        switch (value)
        {
        case 'D':
            return PinType::kDigital;
        case 'A':
            return PinType::kAnalog;
        case 'S':
            return PinType::kServo;
        case 'G':
            return PinType::kMCP4725;
        default:
            return PinType::kNone;
        }
    }

    inline char pinTypeToChar(PinType pinType)
    {
        return static_cast<char>(pinType);
    }
} // namespace PinTypeCodec

namespace RequestSemantics
{
    // The request opcode is derived from the (frame type, pin type) pair.
    inline bool isValidRequest(FrameType frameType, PinType pinType)
    {
        switch (frameType)
        {
        case FrameType::kRead:
            return pinType == PinType::kDigital ||
                   pinType == PinType::kAnalog;
        case FrameType::kWrite:
            return pinType == PinType::kDigital ||
                   pinType == PinType::kAnalog ||
                   pinType == PinType::kServo ||
                   pinType == PinType::kMCP4725;
        default:
            return false;
        }
    }
} // namespace RequestSemantics

namespace PriorityCodec
{
    // Priority uses one byte so the dispatcher can make routing decisions without
    // extra string handling.
    inline Priority PriorityFromChar(char value)
    {
        switch (value)
        {
        case 'L':
            return Priority::kLow;
        case 'H':
            return Priority::kHigh;
        default:
            return Priority::kNone;
        }
    }

    inline char PriorityToChar(Priority Priority)
    {
        return static_cast<char>(Priority);
    }
} // namespace PriorityCodec

namespace ErrorCodeCodec
{
    // Error frames encode the error as a compact numeric field instead of a
    // symbolic character.
    inline ErrorCode errorCodeFromValue(uint8_t value)
    {
        switch (value)
        {
        case 0:
            return ErrorCode::kBadFrame;
        case 1:
            return ErrorCode::kBadPin;
        case 2:
            return ErrorCode::kBadFunction;
        case 3:
            return ErrorCode::kBadPinType;
        case 4:
            return ErrorCode::kBadValue;
        case 5:
            return ErrorCode::kMissingSession;
        case 6:
            return ErrorCode::kUnsupported;
        case 7:
            return ErrorCode::kQueueFull;
        case 8:
            return ErrorCode::kCancelled;
        case 255:
        default:
            return ErrorCode::kNone;
        }
    }

    inline uint8_t errorCodeToValue(ErrorCode errorCode)
    {
        return static_cast<uint8_t>(errorCode);
    }
} // namespace ErrorCodeCodec

#endif // !CODEC_H