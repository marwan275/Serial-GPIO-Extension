#include "analog_pin.h"

#include "pin_session_factory.h"
#include <memory>

namespace
{
    struct AnalogPinFactoryRegistrar
    {
        AnalogPinFactoryRegistrar()
        {
                static constexpr UBaseType_t kRequestQueueDepth = 1024;
                static constexpr uint16_t kWorkerStackDepth = 256;
                static constexpr char kWorkerName[] = "AnalogPin";

                PinSessionFactory::registerFactory(
                    PinType::kAnalog,
                    [](const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue) -> std::unique_ptr<PinSession>
                    {
                        return PinSessionFactory::makeSessionWithResources<AnalogPinSession>(
                            request, globalResponseQueue, kRequestQueueDepth, kWorkerStackDepth, kWorkerName,
                            [](const Frame::RequestFrame &r, PinModeConfig &config) -> bool {
                                config.pinType = r.pin_type;
                                config.resolutionBits = static_cast<uint8_t>(r.value / 100);
                                config.frequency = static_cast<uint32_t>(r.value % 100) * 1000000UL;

                                if (r.type == FrameType::kRead)
                                {
                                    config.pinMode = PinMode::kInput;
                                }
                                else if (r.type == FrameType::kWrite)
                                {
                                    config.pinMode = PinMode::kOutput;
                                }

                                return (config.pinMode == PinMode::kInput || config.pinMode == PinMode::kOutput);
                            });
                    });
        }
    };

    static AnalogPinFactoryRegistrar s_analogPinFactoryRegistrar;
}

AnalogPinSession::AnalogPinSession(uint8_t pinNumber, PinModeConfig config, QueueHandle_t requestQueue,
                                   QueueHandle_t responseQueue, TaskHandle_t workerTask)
    : PinSession(pinNumber, config, requestQueue, responseQueue, workerTask) {}

void AnalogPinSession::init()
{
    if (isInitialized_)
    {
        return;
    }

    if (config_.frequency > 0)
    {
        analogWriteFrequency(pinNumber_, config_.frequency);
    }
    if (config_.resolutionBits > 0)
    {
        analogWriteResolution(config_.resolutionBits);
    }

    if (config_.pinMode == PinMode::kOutput)
    {
        pinMode(pinNumber_, OUTPUT);
        analogWrite(pinNumber_, 0);
    }
    else
    {
        pinMode(pinNumber_, INPUT);
    }
    isInitialized_ = true;
}

void AnalogPinSession::handleRequest(const Frame::RequestFrame &request)
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

    if (config_.pinMode == PinMode::kInput)
    {
        response.value = static_cast<uint16_t>(analogRead(pinNumber_));
        xQueueSend(responseQueue_, &response, portMAX_DELAY);
    }
    else
    {
        analogWrite(pinNumber_, request.value);
    }
}

PinModeConfig AnalogPinSession::pinConfigFromRequest(const Frame::RequestFrame &request) const
{
    return PinModeConfig{
        .pinType = request.pin_type,
        .pinMode = request.type == FrameType::kRead ? PinMode::kInput : PinMode::kOutput,
        .frequency = static_cast<uint32_t>(request.value % 100) * 1000000UL,
        .resolutionBits = static_cast<uint8_t>(request.value / 100)};
}