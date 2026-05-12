#include "pin_session.h"

PinSession::PinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
                       QueueHandle_t responseQueue, TaskHandle_t workerTask)
    : pinNumber_(pinNumber), config_(config), requestQueue_(requestQueue), responseQueue_(responseQueue), workerTask_(workerTask) {}

PinSession::~PinSession()
{
}

const PinModeConfig &PinSession::getPinConfig() const
{
    return config_;
}

void PinSession::applyPinConfig(const PinModeConfig &config)
{
    if (config_ == config)
    {
        return;
    }

    stopWorkerTask();
    config_ = config;
    isInitialized_ = false;
    init();
    startWorkerTask();
}

QueueHandle_t PinSession::getRequestQueueHandle() const
{
    return requestQueue_;
}

TaskHandle_t PinSession::getWorkerTaskHandle() const
{
    return workerTask_;
}

void PinSession::setQueues(QueueHandle_t requestQueue, QueueHandle_t responseQueue)
{
    requestQueue_ = requestQueue;
    responseQueue_ = responseQueue;
}

void PinSession::setWorkerTask(TaskHandle_t workerTask)
{
    workerTask_ = workerTask;
}

void PinSession::startWorkerTask()
{
    // The worker task is expected to be created in a suspended state so the
    // session can control when it starts processing requests.
    if (workerTask_ != nullptr)
    {
        vTaskResume(workerTask_);
    }
}

void PinSession::stopWorkerTask()
{
    // The worker task is expected to be created in a suspended state so the
    // session can control when it stops processing requests.
    if (workerTask_ != nullptr)
    {
        vTaskSuspend(workerTask_);
    }
}

void PinSession::workerLoop(void *pvParameters)
{
    (void)pvParameters;

#if defined(configASSERT)
    configASSERT(requestQueue_ != nullptr);
#endif

    if (!isInitialized_)
    {
        init();
    }

    while (true)
    {
        Frame::RequestFrame request{};

        if (xQueueReceive(requestQueue_, &request, portMAX_DELAY) == pdPASS)
        {
            handleRequest(request);
        }
    }
}

void PinSession::WorkerTaskEntry(void *pvParameters)
{
    auto *session = static_cast<PinSession *>(pvParameters);
    if (session == nullptr)
    {
        vTaskDelete(nullptr);
    }

    session->workerLoop(pvParameters);
    vTaskDelete(nullptr);
}

bool PinSession::supportsRequest(const Frame::RequestFrame &request) const
{
    PinModeConfig requestedConfig = pinConfigFromRequest(request);
    return config_ == requestedConfig;
}

void PinSession::sendOkResponse(uint16_t requestId, uint16_t value) const
{
    if (responseQueue_ == nullptr)
    {
        return;
    }

    Frame::ResponseFrame response{
        .request_id = requestId,
        .value = value,
        .type = FrameType::kResponse,
        .error = ErrorCode::kNone,
    };
    xQueueSend(responseQueue_, &response, portMAX_DELAY);
}