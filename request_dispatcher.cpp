#include "request_dispatcher.h"

#include "Arduino.h"
#include "pin_registry.h"
#include "debug_functions.h"
#include "pin_session_factory.h"
#include <memory>

RequestDispatcher::RequestDispatcher(QueueHandle_t globalRequestQueue, QueueHandle_t globalResponseQueue)
    : globalRequestQueue_(globalRequestQueue), globalResponseQueue_(globalResponseQueue) {}

void RequestDispatcher::run()
{
#if defined(configASSERT)
    configASSERT(globalRequestQueue_ != nullptr);
    configASSERT(globalResponseQueue_ != nullptr);
#endif

    while (true)
    {
        Frame::RequestFrame request{};
        if (xQueueReceive(globalRequestQueue_, &request, portMAX_DELAY) == pdPASS)
        {
            dispatchRequest(request);
        }
    }
}

void RequestDispatcher::dispatchRequest(const Frame::RequestFrame &request)
{
    if (!PinRegistry::isValidPinNumber(request.pin_type, request.pin_number))
    {
        sendErrorResponse(request.request_id, ErrorCode::kBadPin);
        return;
    }

    if (!RequestSemantics::isValidRequest(request.type, request.pin_type))
    {
        sendErrorResponse(request.request_id, ErrorCode::kBadFunction);
        return;
    }

    handleRuntimeRequest(request);
}

void RequestDispatcher::handleRuntimeRequest(const Frame::RequestFrame &request)
{
    PinSession *session = PinRegistry::findSession(request.pin_type, request.pin_number);
    if (session == nullptr)
    {
        if (!createPinSession(request))
        {
            return;
        }

        session = PinRegistry::findSession(request.pin_type, request.pin_number);
        if (session == nullptr)
        {
            sendErrorResponse(request.request_id, ErrorCode::kUnsupported);
            return;
        }
    }

    if (!session->supportsRequest(request))
    {
        sendErrorResponse(request.request_id, ErrorCode::kUnsupported);
        return;
    }

    if (!enqueuePinRequest(*session, request))
    {
        sendErrorResponse(request.request_id, ErrorCode::kQueueFull);
    }
}

bool RequestDispatcher::createPinSession(const Frame::RequestFrame &request)
{
    // Delegate construction of the concrete PinSession (including its
    // request queue and worker task) to the factory.
    std::unique_ptr<PinSession> session = PinSessionFactory::createFromRequest(request, globalResponseQueue_);
    if (!session)
    {
        sendErrorResponse(request.request_id, ErrorCode::kUnsupported);
        return false;
    }

    PinRegistry::storeSession(request.pin_type, request.pin_number, std::move(session));
    return true;
}

bool RequestDispatcher::enqueuePinRequest(PinSession &session, const Frame::RequestFrame &request)
{
    QueueHandle_t requestQueue = session.getRequestQueueHandle();
    if (requestQueue == nullptr)
    {
        return false;
    }

    if (request.priority == Priority::kHigh)
    {
        clearPendingRequests(requestQueue);
    }

    return xQueueSend(requestQueue, &request, 0) == pdPASS;
}

void RequestDispatcher::clearPendingRequests(QueueHandle_t requestQueue) const
{
    if (requestQueue == nullptr)
    {
        return;
    }

    Frame::RequestFrame discardedRequest{};
    while (xQueueReceive(requestQueue, &discardedRequest, 0) == pdPASS)
    {
    }
}

void RequestDispatcher::sendOkResponse(uint16_t requestId, uint16_t value) const
{
    if (globalResponseQueue_ == nullptr)
    {
        return;
    }

    Frame::ResponseFrame response{
        .request_id = requestId,
        .value = value,
        .type = FrameType::kResponse,
        .error = ErrorCode::kNone,
    };
    xQueueSend(globalResponseQueue_, &response, portMAX_DELAY);
}

void RequestDispatcher::sendErrorResponse(uint16_t requestId, ErrorCode error) const
{
    if (globalResponseQueue_ == nullptr)
    {
        return;
    }

    Frame::ResponseFrame response{
        .request_id = requestId,
        .value = 0,
        .type = FrameType::kError,
        .error = error,
    };
    xQueueSend(globalResponseQueue_, &response, portMAX_DELAY);
}