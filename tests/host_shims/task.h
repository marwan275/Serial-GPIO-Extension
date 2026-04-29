#ifndef TESTS_HOST_SHIMS_TASK_H
#define TESTS_HOST_SHIMS_TASK_H

#include "FreeRTOS.h"

using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);

inline BaseType_t xTaskCreate(TaskFunction_t taskCode,
                              const char *taskName,
                              uint16_t stackDepth,
                              void *taskParameters,
                              UBaseType_t priority,
                              TaskHandle_t *createdTask)
{
    (void)taskCode;
    (void)taskName;
    (void)stackDepth;
    (void)taskParameters;
    (void)priority;
    if (createdTask != nullptr)
    {
        *createdTask = reinterpret_cast<TaskHandle_t>(taskCode);
    }
    return pdPASS;
}

inline void vTaskResume(TaskHandle_t taskHandle)
{
    (void)taskHandle;
}

inline void vTaskSuspend(TaskHandle_t taskHandle)
{
    (void)taskHandle;
}

inline void vTaskDelete(TaskHandle_t taskHandle)
{
    (void)taskHandle;
}

#endif // TESTS_HOST_SHIMS_TASK_H