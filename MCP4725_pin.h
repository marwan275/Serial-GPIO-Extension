#ifndef MCP4725_PIN_H
#define MCP4725_PIN_H

#include "pin_session.h"
#include <DFRobot_MCP4725.h>

class MCP4725PinSession : public PinSession
{
public:
    MCP4725PinSession(uint8_t deviceAddress, PinModeConfig config, QueueHandle_t requestQueue,
                      QueueHandle_t responseQueue, TaskHandle_t workerTask);

    void init() override;
    PinModeConfig pinConfigFromRequest(const Frame::RequestFrame &request) const override;

protected:
    void handleRequest(const Frame::RequestFrame &request) override;

private:
    DFRobot_MCP4725 dac_;
};

#endif // MCP4725_PIN_H