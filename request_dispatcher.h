#ifndef REQUEST_DISPATCHER_H
#define REQUEST_DISPATCHER_H

#include "frame.h"
#include "pin_session.h"

class RequestDispatcher
{
public:
    RequestDispatcher(QueueHandle_t globalRequestQueue, QueueHandle_t globalResponseQueue);

    void run();
    void dispatchRequest(const Frame::RequestFrame &request);

private:
    void handleRuntimeRequest(const Frame::RequestFrame &request);

    bool createPinSession(const Frame::RequestFrame &request);
    bool enqueuePinRequest(PinSession &session, const Frame::RequestFrame &request);
    void clearPendingRequests(QueueHandle_t requestQueue) const;

    void sendOkResponse(uint16_t requestId, uint16_t value = 0) const;
    void sendErrorResponse(uint16_t requestId, ErrorCode error) const;

    QueueHandle_t globalRequestQueue_ = nullptr;
    QueueHandle_t globalResponseQueue_ = nullptr;
};

#endif // !REQUEST_DISPATCHER_H