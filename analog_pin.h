#ifndef ANALOG_PIN_H
#define ANALOG_PIN_H

#include "pin_session.h"
class AnalogPinSession : public PinSession
{
public:
    AnalogPinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
                     QueueHandle_t responseQueue, TaskHandle_t workerTask);

    void init() override;
    PinModeConfig pinConfigFromRequest(const Frame::RequestFrame &request) const override;

protected:
    void handleRequest(const Frame::RequestFrame &request) override;
};

#endif // !ANALOG_PIN_H