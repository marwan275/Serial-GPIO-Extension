#include "digital_pin.h"

#if defined(ARDUINO) || defined(TEENSYDUINO)
#include <Arduino.h>
#endif

#include "pin_session_factory.h"
#include <memory>

namespace
{
    struct DigitalPinFactoryRegistrar
    {
        DigitalPinFactoryRegistrar()
        {
                static constexpr UBaseType_t kRequestQueueDepth = 1024;
                static constexpr uint16_t kWorkerStackDepth = 256;
                static constexpr char kWorkerName[] = "DigitalPin";

                PinSessionFactory::registerFactory(
                    PinType::kDigital,
                    [](const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue) -> std::unique_ptr<PinSession>
                    {
                        return PinSessionFactory::makeSessionWithResources<DigitalPinSession>(
                            request, globalResponseQueue, kRequestQueueDepth, kWorkerStackDepth, kWorkerName,
                            [](const Frame::RequestFrame &r, PinModeConfig &config) -> bool {
                                config.pinType = r.pin_type;
                                if (r.pin_type != PinType::kDigital)
                                {
                                    return false;
                                }

                                if (r.type == FrameType::kRead)
                                {
                                    config.pinMode = PinMode::kInput;
                                }
                                else if (r.type == FrameType::kWrite)
                                {
                                    config.pinMode = PinMode::kOutput;
                                }
                                else
                                {
                                    return false;
                                }

                                return (config.pinMode == PinMode::kInput || config.pinMode == PinMode::kOutput);
                            });
                    });
        }
    };

    static DigitalPinFactoryRegistrar s_digitalPinFactoryRegistrar;
}

DigitalPinSession::DigitalPinSession(uint8_t pinNumber, PinModeConfig config,
                                     QueueHandle_t requestQueue, QueueHandle_t responseQueue,
                                     TaskHandle_t workerTask)
    : PinSession(pinNumber, config, requestQueue, responseQueue, workerTask) {}

void DigitalPinSession::init()
{
    if (isInitialized_)
    {
        return;
    }

    if (config_.pinMode == PinMode::kInput)
    {
        pinMode(pinNumber_, INPUT);
    }
    else if (config_.pinMode == PinMode::kOutput)
    {
        pinMode(pinNumber_, OUTPUT);
        digitalWriteFast(pinNumber_, LOW);
    }

    isInitialized_ = true;
}

void DigitalPinSession::handleRequest(const Frame::RequestFrame &request)
{

    if (config_.pinMode == PinMode::kInput)
    {
        if (responseQueue_ == nullptr)
        {
            return;
        }
        Frame::ResponseFrame response{
            .request_id = request.request_id,
            .value = 0,
            .type = FrameType::kResponse,
            .error = ErrorCode::kNone,
        };
        response.value = static_cast<uint16_t>(digitalReadFast(pinNumber_));
        xQueueSend(responseQueue_, &response, portMAX_DELAY);
    }
    else if (config_.pinMode == PinMode::kOutput)
    {
        digitalWriteFast(pinNumber_, request.value ? HIGH : LOW);
    }
}

PinModeConfig DigitalPinSession::pinConfigFromRequest(const Frame::RequestFrame &request) const
{
    return PinModeConfig{
        .pinType = request.pin_type,
        .pinMode = request.type == FrameType::kRead ? PinMode::kInput : PinMode::kOutput,
        .frequency = 0,
        .resolutionBits = 0};
}
