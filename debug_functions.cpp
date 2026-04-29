#include "debug_functions.h"
#include "Arduino.h"

void sendDebugResponse(uint16_t debug_value, QueueHandle_t queue)
{
    if (queue == nullptr)
    {
        return;
    }

    Frame::ResponseFrame response{
        .request_id = 0,
        .value = debug_value,
        .type = FrameType::kDebug,
        .error = ErrorCode::kNone,
    };
    xQueueSend(queue, &response, portMAX_DELAY);
}

void SerialSendDebugFrame(const char *message)
{
    if (message == nullptr)
    {
        return;
    }

    Serial.write(FrameChar::kStartFrame);
    Serial.write(FrameTypeCodec::frameTypeToChar(FrameType::kDebug));
    Serial.write(FrameChar::kDelimiter);
    Serial.print(message);
    Serial.write(FrameChar::kEndFrame);
    Serial.write(FrameChar::kEndLine);
}
