#ifndef PIN_SESSION_H
#define PIN_SESSION_H

#include "frame.h"
#include "codec.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <Arduino.h>

struct PinModeConfig
{
    PinType pinType = PinType::kNone;
    PinMode pinMode = PinMode::kNone;
};

inline bool operator==(const PinModeConfig &lhs, const PinModeConfig &rhs)
{
    return lhs.pinType == rhs.pinType &&
           lhs.pinMode == rhs.pinMode;
}

inline bool operator!=(const PinModeConfig &lhs, const PinModeConfig &rhs)
{
    return !(lhs == rhs);
}

class PinSession
{
public:
    PinSession &operator=(const PinSession &) = delete;
    PinSession(const PinSession &) = delete;

    PinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
               QueueHandle_t responseQueue, TaskHandle_t workerTask);

    virtual ~PinSession();

    const PinModeConfig &getPinConfig() const;

    void applyPinConfig(const PinModeConfig &config);

    QueueHandle_t getRequestQueueHandle() const;

    TaskHandle_t getWorkerTaskHandle() const;

    void setQueues(QueueHandle_t requestQueue, QueueHandle_t responseQueue);

    void setWorkerTask(TaskHandle_t workerTask);

    void startWorkerTask();
    void stopWorkerTask();

    bool supportsRequest(const Frame::RequestFrame &request) const;

    void sendOkResponse(uint16_t requestId, uint16_t value = 0) const;

    static void WorkerTaskEntry(void *pvParameters);

    virtual void init() = 0;
    virtual void workerLoop(void *pvParameters);
    virtual PinModeConfig pinConfigFromRequest(const Frame::RequestFrame &request) const = 0;

protected:
    virtual void handleRequest(const Frame::RequestFrame &request) = 0;

    bool isInitialized_ = false;
    const uint8_t pinNumber_;
    PinModeConfig config_;
    QueueHandle_t requestQueue_ = nullptr;
    QueueHandle_t responseQueue_ = nullptr;
    TaskHandle_t workerTask_ = nullptr;
};

#endif // !PIN_SESSION_H