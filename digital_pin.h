#ifndef DIGITAL_PIN_H
#define DIGITAL_PIN_H

#include "pin_session.h"
#include "debug_functions.h"

class DigitalPinSession : public PinSession
{
public:
    DigitalPinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
                      QueueHandle_t responseQueue, TaskHandle_t workerTask);

    void init() override;
    PinModeConfig pinConfigFromRequest(const Frame::RequestFrame &request) const override;

protected:
    void handleRequest(const Frame::RequestFrame &request) override;
};

#endif // !DIGITAL_PIN_H