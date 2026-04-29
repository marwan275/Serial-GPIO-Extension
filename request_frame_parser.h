#ifndef REQUEST_FRAME_PARSER_H
#define REQUEST_FRAME_PARSER_H

#if defined(ARDUINO) || defined(TEENSYDUINO)
#include <FreeRTOS.h>
#include <queue.h>
#include <Stream.h>
#else
#include "tests/host_shims/FreeRTOS.h"
#include "tests/host_shims/queue.h"
#include "tests/host_shims/Stream.h"
#endif
#include <array>
#include "frame.h"

// RequestFrameParser incrementally consumes serial bytes, recognizes the
// @...;\n request envelope, and pushes validated RequestFrame structs into the
// shared queue.
// RequestFrame format:
// @<type>,<requestId>,<pinType>,<priority>,<pin>,<value>;\n
class RequestFrameParser
{
public:
    struct Statistics
    {
        // Counters are monotonic for the life of the parser instance so the RX
        // task can be sampled without resetting parser state.
        uint32_t bytes_consumed = 0;
        uint32_t frames_queued = 0;
        uint32_t malformed_frames = 0;
        uint32_t overflow_drops = 0;
        uint32_t queue_send_failures = 0;
    };

    RequestFrameParser(QueueHandle_t RequestQueue, Stream *inputStream);

    // Parses a bounded chunk of currently available serial data.
    void parse();

    // Exposes transport health counters for diagnostics.
    const Statistics &statistics() const;

private:
    // Moves bytes from the stream into the payload buffer while enforcing frame
    // boundaries and line ending rules.
    bool fillBuffer();

    // Decodes the buffered payload into a typed RequestFrame once a complete
    // frame has been received.
    bool constructFrame(Frame::RequestFrame &frame) const;

    QueueHandle_t RequestQueue_;
    Stream *inputStream_;
    std::array<char, Frame::kRequestFramePayloadMaxChars> buffer_;
    size_t bufferLength_ = 0;
    bool inFrame_ = false;
    bool awaitingLineEnd_ = false;
    Statistics statistics_{};
};
#endif // !REQUEST_FRAME_PARSER_H
