#ifndef FRAME_H
#define FRAME_H

#include <cstddef>

#include "codec.h"

namespace FrameChar
{
    // These are the fixed framing bytes shared by every frame.
    constexpr uint8_t kStartFrame = '@';
    constexpr uint8_t kEndFrame = ';';
    constexpr uint8_t kDelimiter = ',';
    constexpr uint8_t kEndLine = '\n';
} // namespace FrameChar

namespace Frame
{

    constexpr size_t kFramingCharCount = 2; // '@' and ';'
    constexpr size_t kLineEndingCharCount = 1; // '\n'

    // The parser stores request frames as compact fixed-size structs after the
    // ASCII payload has been validated and decoded.
    // RequestFrame format:
    // @<type>,<requestId>,<pinType>,<priority>,<pin>,<value>;
    // @W,2,D,H,13,11;
    constexpr size_t kRequestFrameBinarySize = 8;
    constexpr size_t kRequestFrameFieldCount = 6;
    constexpr size_t kRequestFrameMaxChars = 23;              // includes '@' and ';', excludes '\n'
    constexpr size_t kRequestFrameMaxCharsWithLineEnd = 24;   // includes '@', ';', and '\n'
    constexpr size_t kRequestFramePayloadMaxChars = 21;       // between '@' and ';'
    struct RequestFrame
    {
        uint16_t request_id;
        uint16_t value;
        FrameType type; // kRead or kWrite
        Priority priority;
        PinType pin_type;
        uint8_t pin_number;
    };

    static_assert(sizeof(RequestFrame) == kRequestFrameBinarySize, "RequestFrame size must stay in sync with queue item layout");

    // ResponseFrame is the outbound queue payload used by the eventual TX task.
    // ResponseFrame format:
    // @<type>,<requestId>,<value>,<error>;\n
    constexpr size_t kResponseFrameBinarySize = 6;
    constexpr size_t kResponseFrameFieldCount = 4;
    struct ResponseFrame
    {
        uint16_t request_id;
        uint16_t value;
        FrameType type;  // kResponse or kError
        ErrorCode error; // valid only when type == kError
    };

    static_assert(sizeof(ResponseFrame) == kResponseFrameBinarySize, "ResponseFrame must be exactly 6 bytes");

} // namespace Frame

#endif // !FRAME_H