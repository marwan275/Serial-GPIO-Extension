#include "app_tasks.h"
#include "request_dispatcher.h"
#include "request_frame_parser.h"
#include "debug_functions.h"
#include <Arduino.h>
#include <queue.h>

namespace ApplicationQueues
{
    // These queues are the handoff between serial ingress, command execution,
    // and serial egress.
    static QueueHandle_t GlobalRequestQueue = nullptr;
    static QueueHandle_t GlobalResponseQueue = nullptr;

    bool createQueues()
    {
        GlobalRequestQueue = xQueueCreate(kRequestQueueLength, kRequestQueueItemSize);
        if (GlobalRequestQueue == nullptr)
            return false;

        GlobalResponseQueue = xQueueCreate(kResponseQueueLength, kResponseQueueItemSize);
        if (GlobalResponseQueue == nullptr)
        {
            vQueueDelete(GlobalRequestQueue);
            return false;
        }
        return true;
    }
}

namespace
{
    constexpr TickType_t kSerialRxIdleDelay = pdMS_TO_TICKS(1);

    // SerialRxTask owns USB serial ingress and hands complete RequestFrame
    // objects to the rest of the runtime.
    void SerialRxTask(void *pvParameters)
    {
        (void)pvParameters;
        if (ApplicationQueues::GlobalRequestQueue == nullptr)
        {
            vTaskDelete(nullptr);
        }

        RequestFrameParser request_parser(ApplicationQueues::GlobalRequestQueue, &Serial);
        for (;;)
        {
            if (Serial.available() > 0)
            {
                request_parser.parse();
            }
            else
            {
                vTaskDelay(kSerialRxIdleDelay);
            }
        }
    }

    // Command dispatch owns pin lifecycle, validation, and routing into per-pin workers.
    void CommandDispatcherTask(void *pvParameters)
    {
        (void)pvParameters;
        if (ApplicationQueues::GlobalRequestQueue == nullptr || ApplicationQueues::GlobalResponseQueue == nullptr)
        {
            vTaskDelete(nullptr);
        }

        RequestDispatcher dispatcher(ApplicationQueues::GlobalRequestQueue, ApplicationQueues::GlobalResponseQueue);
        dispatcher.run();
    }

    // SerialTxTask will become the single writer to the USB serial port.
    void SerialTxTask(void *pvParameters)
    {
        (void)pvParameters;
        if (ApplicationQueues::GlobalResponseQueue == nullptr)
        {
            vTaskDelete(nullptr);
        }

        for (;;)
        {
            Frame::ResponseFrame response{};
            if (xQueueReceive(ApplicationQueues::GlobalResponseQueue, &response, portMAX_DELAY) != pdPASS)
            {
                continue;
            }

            Serial.write(FrameChar::kStartFrame);
            Serial.write(FrameTypeCodec::frameTypeToChar(response.type));
            Serial.write(FrameChar::kDelimiter);
            Serial.print(response.request_id);
            Serial.write(FrameChar::kDelimiter);
            Serial.print(response.value);
            Serial.write(FrameChar::kDelimiter);
            Serial.print(ErrorCodeCodec::errorCodeToValue(response.error));
            Serial.write(FrameChar::kEndFrame);
            Serial.write(FrameChar::kEndLine);
        }
    }
}

namespace ApplicationTasks
{
    // Task handles are kept in this translation unit because no other module
    // currently needs to suspend or inspect the tasks directly.
    static TaskHandle_t SerialRxTaskHandle;
    static TaskHandle_t CommandDispatcherTaskHandle;
    static TaskHandle_t SerialTxTaskHandle;

    constexpr char SerialRxTaskName[] = "SerialRxTask";
    constexpr UBaseType_t SerialRxTaskPriority = configMAX_PRIORITIES - 1;
    constexpr size_t SerialRxTaskStackDepth = 256;

    constexpr char CommandDispatcherTaskName[] = "CommandDispatcherTask";
    constexpr UBaseType_t CommandDispatcherTaskPriority = configMAX_PRIORITIES - 1;
    constexpr size_t CommandDispatcherTaskStackDepth = 256;

    constexpr char SerialTxTaskName[] = "SerialTxTask";
    constexpr UBaseType_t SerialTxTaskPriority = configMAX_PRIORITIES - 1;
    constexpr size_t SerialTxTaskStackDepth = 256;

    bool createTasks()
    {
        BaseType_t result;

        // Serial RX runs at the highest priority so frame parsing keeps up with
        // host traffic and does not lose bytes to backpressure.
        result = xTaskCreate(SerialRxTask, SerialRxTaskName, SerialRxTaskStackDepth, nullptr, SerialRxTaskPriority, &SerialRxTaskHandle);
        if (result != pdPASS)
            return false;

        // Dispatcher work is control-plane logic and can run below the serial RX
        // task without delaying byte ingestion.
        result = xTaskCreate(CommandDispatcherTask, CommandDispatcherTaskName, CommandDispatcherTaskStackDepth, nullptr, CommandDispatcherTaskPriority, &CommandDispatcherTaskHandle);
        if (result != pdPASS)
        {
            vTaskDelete(SerialRxTaskHandle);
            return false;
        }

        // TX is intentionally the lowest-priority task because responses are less
        // time-sensitive than ingress parsing and dispatch decisions.
        result = xTaskCreate(SerialTxTask, SerialTxTaskName, SerialTxTaskStackDepth, nullptr, SerialTxTaskPriority, &SerialTxTaskHandle);
        if (result != pdPASS)
        {
            vTaskDelete(SerialRxTaskHandle);
            vTaskDelete(CommandDispatcherTaskHandle);
            return false;
        }
        return true;
    }
}
