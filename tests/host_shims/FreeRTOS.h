#ifndef TESTS_HOST_SHIMS_FREERTOS_H
#define TESTS_HOST_SHIMS_FREERTOS_H

#include <stdint.h>

using BaseType_t = int;
using TickType_t = uint32_t;
using UBaseType_t = unsigned int;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;

#define configUSE_QUEUE_SETS 1

#endif // TESTS_HOST_SHIMS_FREERTOS_H