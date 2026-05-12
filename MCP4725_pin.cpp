#include "MCP4725_pin.h"

#include "pin_session_factory.h"
#include <memory>

namespace
{
    constexpr uint16_t kMcp4725QueueDepth = 1024;
    constexpr uint16_t kMcp4725WorkerStackDepth = 256;
    constexpr char kMcp4725WorkerName[] = "MCP4725Pin";
    constexpr uint16_t kMcp4725ReferenceMillivolts = 5000;
    constexpr uint16_t kMcp4725MaxMillivolts = 5000;

    struct MCP4725PinFactoryRegistrar
    {
        MCP4725PinFactoryRegistrar()
        {
            PinSessionFactory::registerFactory(
                PinType::kMCP4725,
                [](const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue) -> std::unique_ptr<PinSession>
                {
                    return PinSessionFactory::makeSessionWithResources<MCP4725PinSession>(
                        request, globalResponseQueue, kMcp4725QueueDepth, kMcp4725WorkerStackDepth, kMcp4725WorkerName,
                        [](const Frame::RequestFrame &r, PinModeConfig &config) -> bool
                        {
                            if (r.pin_type != PinType::kMCP4725 || r.type != FrameType::kWrite)
                            {
                                return false;
                            }

                            config.pinType = r.pin_type;
                            config.pinMode = PinMode::kOutput;
                            return true;
                        });
                });
        }
    };

    static MCP4725PinFactoryRegistrar s_mcp4725PinFactoryRegistrar;
} // namespace

MCP4725PinSession::MCP4725PinSession(uint8_t deviceAddress, PinModeConfig config, QueueHandle_t requestQueue,
                                     QueueHandle_t responseQueue, TaskHandle_t workerTask)
    : PinSession(deviceAddress, config, requestQueue, responseQueue, workerTask)
{
}

void MCP4725PinSession::init()
{
    if (isInitialized_)
    {
        return;
    }

    dac_.init(pinNumber_, kMcp4725ReferenceMillivolts);
    isInitialized_ = true;
}

void MCP4725PinSession::handleRequest(const Frame::RequestFrame &request)
{
    if (!isInitialized_)
    {
        init();
    }

    if (!isInitialized_)
    {
        return;
    }

    const uint16_t outputMillivolts = request.value > kMcp4725MaxMillivolts ? kMcp4725MaxMillivolts : request.value;
    dac_.outputVoltage(outputMillivolts);
    sendOkResponse(request.request_id, outputMillivolts);
}

PinModeConfig MCP4725PinSession::pinConfigFromRequest(const Frame::RequestFrame &request) const
{
    if (request.pin_type != PinType::kMCP4725 || request.type != FrameType::kWrite)
    {
        return PinModeConfig{};
    }

    return PinModeConfig{
        .pinType = request.pin_type,
        .pinMode = PinMode::kOutput,
    };
}