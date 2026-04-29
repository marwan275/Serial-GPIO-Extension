#include "request_frame_parser.h"

namespace
{
    // Reads are batched so the parser stays non-blocking without paying the cost of
    // a per-byte Stream read on every loop iteration.
    constexpr size_t kReadChunkSize = 64;
    constexpr size_t kMaxBytesPerParseCall = 256;
} // namespace

RequestFrameParser::RequestFrameParser(QueueHandle_t RequestQueue, Stream *inputStream)
    : RequestQueue_(RequestQueue), inputStream_(inputStream)
{
    if (inputStream_ != nullptr)
    {
        inputStream_->setTimeout(0);
    }
}

void RequestFrameParser::parse()
{
    // The public API intentionally hides the parser's internal state machine.
    (void)fillBuffer();
}

const RequestFrameParser::Statistics &RequestFrameParser::statistics() const
{
    return statistics_;
}

bool RequestFrameParser::fillBuffer()
{
    if (inputStream_ == nullptr)
    {
        return false;
    }

    bool madeProgress = false;
    size_t remainingBudget = kMaxBytesPerParseCall;
    std::array<char, kReadChunkSize> readBuffer;

    while (remainingBudget > 0)
    {
        const int availableBytes = inputStream_->available();
        if (availableBytes <= 0)
        {
            break;
        }

        size_t bytesToRead = static_cast<size_t>(availableBytes);
        if (bytesToRead > readBuffer.size())
        {
            bytesToRead = readBuffer.size();
        }
        if (bytesToRead > remainingBudget)
        {
            bytesToRead = remainingBudget;
        }

        const size_t bytesRead = inputStream_->readBytes(readBuffer.data(), bytesToRead);
        if (bytesRead == 0)
        {
            break;
        }

        madeProgress = true;
        remainingBudget -= bytesRead;
        statistics_.bytes_consumed += bytesRead;

        for (size_t index = 0; index < bytesRead; ++index)
        {
            const char byte = readBuffer[index];

            if (!inFrame_)
            {
                // Ignore everything until the next explicit frame start marker.
                if (byte == FrameChar::kStartFrame)
                {
                    bufferLength_ = 0;
                    inFrame_ = true;
                    awaitingLineEnd_ = false;
                }
                continue;
            }

            if (awaitingLineEnd_)
            {
                // ';' has already been seen, so only CRLF line termination is
                // accepted from this point onward.
                if (byte == FrameChar::kEndLine)
                {
                    Frame::RequestFrame frame{};
                    if (constructFrame(frame))
                    {
                        if (RequestQueue_ != nullptr && xQueueSend(RequestQueue_, &frame, 0) == pdPASS)
                        {
                            ++statistics_.frames_queued;
                        }
                        else
                        {
                            ++statistics_.queue_send_failures;
                        }
                    }
                    else
                    {
                        ++statistics_.malformed_frames;
                    }

                    bufferLength_ = 0;
                    inFrame_ = false;
                    awaitingLineEnd_ = false;
                    continue;
                }

                if (byte == '\r')
                {
                    continue;
                }

                ++statistics_.malformed_frames;
                bufferLength_ = 0;
                awaitingLineEnd_ = false;
                inFrame_ = (byte == FrameChar::kStartFrame);
                continue;
            }

            if (byte == FrameChar::kStartFrame)
            {
                // A fresh '@' mid-frame means the old partial payload is lost,
                // but the new start marker can still begin a valid frame.
                ++statistics_.malformed_frames;
                bufferLength_ = 0;
                inFrame_ = true;
                awaitingLineEnd_ = false;
                continue;
            }

            if (byte == FrameChar::kEndFrame)
            {
                awaitingLineEnd_ = true;
                continue;
            }

            if (byte == FrameChar::kEndLine || byte == '\r')
            {
                ++statistics_.malformed_frames;
                bufferLength_ = 0;
                inFrame_ = false;
                awaitingLineEnd_ = false;
                continue;
            }

            if (bufferLength_ >= buffer_.size())
            {
                // A payload that exceeds the fixed request budget is dropped so
                // the RX path cannot overrun its local storage.
                ++statistics_.overflow_drops;
                bufferLength_ = 0;
                inFrame_ = false;
                awaitingLineEnd_ = false;
                continue;
            }

            buffer_[bufferLength_] = byte;
            ++bufferLength_;
        }
    }

    return madeProgress;
}

bool RequestFrameParser::constructFrame(Frame::RequestFrame &frame) const
{
    if (bufferLength_ == 0)
    {
        return false;
    }

    Frame::RequestFrame parsedFrame{};
    size_t fieldIndex = 0;
    size_t fieldLength = 0;
    uint32_t numericValue = 0;

    // One linear scan handles both field splitting and numeric accumulation
    for (size_t index = 0; index <= bufferLength_; ++index)
    {
        const char byte = (index == bufferLength_) ? static_cast<char>(FrameChar::kDelimiter) : buffer_[index];

        if (byte == FrameChar::kDelimiter)
        {
            if (fieldLength == 0 || fieldIndex >= Frame::kRequestFrameFieldCount)
            {
                return false;
            }

            switch (fieldIndex)
            {
            case 0:
                parsedFrame.type = FrameTypeCodec::frameTypeFromChar(buffer_[index - 1]);
                if (parsedFrame.type == FrameType::kNone)
                {
                    return false;
                }
                break;
            case 1:
                parsedFrame.request_id = static_cast<uint16_t>(numericValue);
                break;
            case 2:
                parsedFrame.pin_type = PinTypeCodec::pinTypeFromChar(buffer_[index - 1]);
                if (parsedFrame.pin_type == PinType::kNone)
                {
                    return false;
                }
                break;
            case 3:
                parsedFrame.priority = PriorityCodec::PriorityFromChar(buffer_[index - 1]);
                if (parsedFrame.priority == Priority::kNone)
                {
                    return false;
                }
                break;
            case 4:
                parsedFrame.pin_number = static_cast<uint8_t>(numericValue);
                break;
            case 5:
                parsedFrame.value = static_cast<uint16_t>(numericValue);
                break;
            default:
                return false;
            }

            ++fieldIndex;
            fieldLength = 0;
            numericValue = 0;
            continue;
        }

        switch (fieldIndex)
        {
        case 0:
        case 2:
        case 3:
            if (fieldLength != 0)
            {
                return false;
            }
            ++fieldLength;
            break;
        case 1:
        case 5:
            if (byte < '0' || byte > '9')
            {
                return false;
            }
            numericValue = numericValue * 10 + static_cast<uint32_t>(byte - '0');
            if (numericValue > UINT16_MAX)
            {
                return false;
            }
            ++fieldLength;
            break;
        case 4:
            if (byte < '0' || byte > '9')
            {
                return false;
            }
            numericValue = numericValue * 10 + static_cast<uint32_t>(byte - '0');
            if (numericValue > UINT8_MAX)
            {
                return false;
            }
            ++fieldLength;
            break;
        default:
            return false;
        }
    }

    if (fieldIndex != Frame::kRequestFrameFieldCount)
    {
        return false;
    }

    if (!RequestSemantics::isValidRequest(parsedFrame.type, parsedFrame.pin_type))
    {
        return false;
    }

    // Commit only after the entire payload has passed validation.
    frame = parsedFrame;
    return true;
}