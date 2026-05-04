#include "servo_pin.h"
#include <Arduino.h>
#include "pin_session_factory.h"
#include <memory>

namespace
{
    struct ServoPinFactoryRegistrar
    {
        ServoPinFactoryRegistrar()
        {
            static constexpr UBaseType_t kRequestQueueDepth = 1024;
            static constexpr uint16_t kWorkerStackDepth = 256;
            static constexpr char kWorkerName[] = "ServoPin";

            PinSessionFactory::registerFactory(
                PinType::kServo,
                [](const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue) -> std::unique_ptr<PinSession>
                {
                    return PinSessionFactory::makeSessionWithResources<ServoPinSession>(
                        request, globalResponseQueue, kRequestQueueDepth, kWorkerStackDepth, kWorkerName,
                        [](const Frame::RequestFrame &r, PinModeConfig &config) -> bool
                        {
                            config.pinType = r.pin_type;

                            if (r.pin_type == PinType::kServo)
                            {
                                if (r.type == FrameType::kRead)
                                {
                                    return false;
                                }
                                else if (r.type == FrameType::kWrite)
                                {
                                    config.pinMode = PinMode::kOutput;
                                }
                            }
                            else
                            {
                                return false;
                            }

                            return (config.pinMode == PinMode::kOutput);
                        });
                });
        }
    };

    static ServoPinFactoryRegistrar s_servoPinFactoryRegistrar;
}

ServoPinSession::ServoPinSession(uint8_t pinNumber, PinModeConfig config,
                                 QueueHandle_t requestQueue, QueueHandle_t responseQueue,
                                 TaskHandle_t workerTask)
    : PinSession(pinNumber, config, requestQueue, responseQueue, workerTask) {}

void ServoPinSession::init()
{
    if (isInitialized_)
    {
        return;
    }

    servo_motor_.attach(pinNumber_);

    isInitialized_ = true;
}

void ServoPinSession::handleRequest(const Frame::RequestFrame &request)
{
    servo_motor_.write(request.value);
}

PinModeConfig ServoPinSession::pinConfigFromRequest(const Frame::RequestFrame &request) const
{
    return PinModeConfig{
        .pinType = request.pin_type,
    .pinMode = request.type == FrameType::kRead ? PinMode::kInput : PinMode::kOutput};
}
