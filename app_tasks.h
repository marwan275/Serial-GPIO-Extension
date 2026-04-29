#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "arduino_freertos.h"
#include "frame.h"

namespace ApplicationTasks
{
    // Creates the firmware's long-lived task graph.
    bool createTasks();
}

namespace ApplicationQueues
{
    // Request and response traffic stays in small fixed queues so the firmware
    // can avoid heap growth after startup.
    constexpr size_t kRequestQueueLength = 1024 * 2;
    constexpr size_t kRequestQueueItemSize = sizeof(Frame::RequestFrame);
    constexpr size_t kResponseQueueLength = 1024 * 2;
    constexpr size_t kResponseQueueItemSize = sizeof(Frame::ResponseFrame);

    // Allocates the global transport queues used by the application tasks.
    bool createQueues();
}
#endif // APP_TASKS_H