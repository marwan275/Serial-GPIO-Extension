#include "../request_frame_parser.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if !defined(ARDUINO) && !defined(TEENSYDUINO) && !defined(REQUEST_FRAME_PARSER_TEST_EXTERNAL_IMPL)
#include "../request_frame_parser.cpp"
#endif

namespace
{
class FakeStream : public Stream
{
public:
    void append(const std::string &chunk)
    {
        input_ += chunk;
    }

    int available() override
    {
        return static_cast<int>(input_.size() - readOffset_);
    }

    size_t readBytes(char *buffer, size_t length) override
    {
        const size_t readable = input_.size() - readOffset_;
        const size_t bytesToRead = (length < readable) ? length : readable;
        for (size_t index = 0; index < bytesToRead; ++index)
        {
            buffer[index] = input_[readOffset_ + index];
        }

        readOffset_ += bytesToRead;
        return bytesToRead;
    }

private:
    std::string input_;
    size_t readOffset_ = 0;
};

struct ExpectedFrame
{
    const char *name;
    Frame::RequestFrame frame;
};

bool framesEqual(const Frame::RequestFrame &left, const Frame::RequestFrame &right)
{
    return left.request_id == right.request_id &&
           left.value == right.value &&
           left.type == right.type &&
           left.priority == right.priority &&
           left.pin_type == right.pin_type &&
           left.pin_number == right.pin_number;
}

std::string describeFrame(const Frame::RequestFrame &frame)
{
    std::ostringstream output;
    output << "@" << FrameTypeCodec::frameTypeToChar(frame.type)
           << "," << frame.request_id
           << "," << PinTypeCodec::pinTypeToChar(frame.pin_type)
           << "," << PriorityCodec::PriorityToChar(frame.priority)
           << "," << static_cast<int>(frame.pin_number)
           << "," << frame.value
           << ";";
    return output.str();
}

void printStatistics(const RequestFrameParser::Statistics &statistics)
{
    std::cout << "Statistics\n";
    std::cout << "  bytes_consumed: " << statistics.bytes_consumed << "\n";
    std::cout << "  frames_queued: " << statistics.frames_queued << "\n";
    std::cout << "  malformed_frames: " << statistics.malformed_frames << "\n";
    std::cout << "  overflow_drops: " << statistics.overflow_drops << "\n";
    std::cout << "  queue_send_failures: " << statistics.queue_send_failures << "\n";
}
} // namespace

int main()
{
    QueueHandle_t requestQueue = xQueueCreate(16, sizeof(Frame::RequestFrame));
    if (requestQueue == nullptr)
    {
        std::cerr << "Failed to create host request queue\n";
        return EXIT_FAILURE;
    }

    FakeStream stream;
    RequestFrameParser parser(requestQueue, &stream);

    const std::vector<std::string> chunks = {
        "noise@X,1,D,H,",
        "13,1;\n@W,2,D,L,13,0;\n",
        "@R,3,D,L,13,0;\r\n",
        "@W,4,D,H,13,70000;\n",
        "@W,5,D,H,123456789012345678901,1;\n",
        "@W,6,D,H,13,1;X\n",
        "@W,7,A,H,13,1;\n",
        "tail"};

    for (const std::string &chunk : chunks)
    {
        stream.append(chunk);
        parser.parse();
    }

    const std::array<ExpectedFrame, 2> expectedFrames = {{
                                                            {
                                                                "write frame",
                                                                {2, 0, FrameType::kWrite, Priority::kLow, PinType::kDigital, 13},
                                                            },
                                                            {
                                                                "read frame",
                                                                {3, 0, FrameType::kRead, Priority::kLow, PinType::kDigital, 13},
                                                            }}};

    bool success = true;
    const RequestFrameParser::Statistics &statistics = parser.statistics();

    if (uxQueueMessagesWaiting(requestQueue) != expectedFrames.size())
    {
        std::cerr << "Expected " << expectedFrames.size() << " queued frames, got "
                  << uxQueueMessagesWaiting(requestQueue) << "\n";
        success = false;
    }

    if (statistics.frames_queued != expectedFrames.size())
    {
        std::cerr << "Expected frames_queued == " << expectedFrames.size() << ", got "
                  << statistics.frames_queued << "\n";
        success = false;
    }

    if (statistics.malformed_frames != 4)
    {
        std::cerr << "Expected malformed_frames == 4, got " << statistics.malformed_frames << "\n";
        success = false;
    }

    if (statistics.overflow_drops != 1)
    {
        std::cerr << "Expected overflow_drops == 1, got " << statistics.overflow_drops << "\n";
        success = false;
    }

    if (statistics.queue_send_failures != 0)
    {
        std::cerr << "Expected queue_send_failures == 0, got " << statistics.queue_send_failures << "\n";
        success = false;
    }

    std::cout << "Dequeued frames\n";
    for (const ExpectedFrame &expected : expectedFrames)
    {
        Frame::RequestFrame actual{};
        if (!hostQueuePop(requestQueue, actual))
        {
            std::cerr << "Missing queued frame for " << expected.name << "\n";
            success = false;
            break;
        }

        std::cout << "  " << expected.name << ": " << describeFrame(actual) << "\n";
        if (!framesEqual(actual, expected.frame))
        {
            std::cerr << "Frame mismatch for " << expected.name << "\n";
            std::cerr << "  expected: " << describeFrame(expected.frame) << "\n";
            std::cerr << "  actual:   " << describeFrame(actual) << "\n";
            success = false;
        }
    }

    printStatistics(statistics);
    vQueueDelete(requestQueue);

    if (!success)
    {
        return EXIT_FAILURE;
    }

    std::cout << "Parser host test passed\n";
    return EXIT_SUCCESS;
}