#ifndef DEBUG_FUNCTIONS_H
#define DEBUG_FUNCTIONS_H

#include "codec.h"
#include <FreeRTOS.h>
#include "frame.h"
#include <queue.h>

// #define DEBUG

void sendDebugResponse(uint16_t debug_value, QueueHandle_t queue);
void SerialSendDebugFrame(const char *message);

#endif // DEBUG_FUNCTIONS_H
