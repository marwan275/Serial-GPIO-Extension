#ifndef PIN_SESSION_FACTORY_H
#define PIN_SESSION_FACTORY_H

#include "frame.h"
#include "pin_session.h"
#include <memory>
#include <functional>
#include <cstdint>

class PinSessionFactory
{
public:
    // The factory is responsible for creating any per-session resources
    // (request queue, worker task, etc.) and returning a fully-constructed
    // PinSession instance. The factory receives the request and the
    // global response queue and must return nullptr on failure.
    using FactoryFunc = std::function<std::unique_ptr<PinSession>(const Frame::RequestFrame &, QueueHandle_t)>;

    // Register a factory for a specific PinType. Returns true on success.
    static bool registerFactory(PinType pinType, FactoryFunc factory);

    // Create a PinSession for the given request using a registered factory.
    // The factory will create the session's request queue and worker task as
    // needed. On success returns a non-null unique_ptr owning the created
    // session. On failure returns nullptr.
    static std::unique_ptr<PinSession> createFromRequest(const Frame::RequestFrame &request,
                                                         QueueHandle_t globalResponseQueue);

    // Helper: create a PinSession of type `SessionT` and the per-session
    // resources (request queue and worker task). `configBuilder` is a
    // callable taking `(const Frame::RequestFrame&, PinModeConfig&)` and
    // returns `bool` indicating whether a valid `PinModeConfig` was built.
    template <typename SessionT, typename ConfigBuilder>
    static std::unique_ptr<PinSession> makeSessionWithResources(const Frame::RequestFrame &request,
                                                                QueueHandle_t globalResponseQueue,
                                                                UBaseType_t requestQueueDepth,
                                                                uint16_t workerStackDepth,
                                                                const char *workerName,
                                                                ConfigBuilder configBuilder)
    {
        // Create queue
        QueueHandle_t requestQueue = xQueueCreate(requestQueueDepth, sizeof(Frame::RequestFrame));
        if (requestQueue == nullptr)
        {
            return nullptr;
        }

        PinModeConfig config{};
        if (!configBuilder(request, config))
        {
            vQueueDelete(requestQueue);
            return nullptr;
        }

        auto session = std::make_unique<SessionT>(request.pin_number, config, requestQueue, globalResponseQueue, nullptr);
        if (!session)
        {
            vQueueDelete(requestQueue);
            return nullptr;
        }

        TaskHandle_t workerTask = nullptr;
        if (xTaskCreate(PinSession::WorkerTaskEntry, workerName, workerStackDepth, session.get(), configMAX_PRIORITIES - 1, &workerTask) != pdPASS)
        {
            vQueueDelete(requestQueue);
            return nullptr;
        }

        session->setWorkerTask(workerTask);
        return session;
    }
};

#endif // PIN_SESSION_FACTORY_H
