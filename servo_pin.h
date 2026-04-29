#ifndef SERVO_PIN_H
#define SERVO_PIN_H

#include "pin_session.h"
#include "debug_functions.h"
#include <Servo.h>

class ServoPinSession : public PinSession
{
public:
    ServoPinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
                    QueueHandle_t responseQueue, TaskHandle_t workerTask);

    void init() override;
    PinModeConfig pinConfigFromRequest(const Frame::RequestFrame &request) const override;

protected:
    void handleRequest(const Frame::RequestFrame &request) override;
    Servo servo_motor_;
};

#endif // !SERVO_PIN_H