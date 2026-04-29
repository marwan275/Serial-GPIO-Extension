#ifndef TESTS_HOST_SHIMS_QUEUE_H
#define TESTS_HOST_SHIMS_QUEUE_H

#include "FreeRTOS.h"

#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <utility>
#include <vector>

struct HostQueue
{
    size_t item_size = 0;
    std::vector<std::vector<uint8_t>> items;
};

using QueueHandle_t = HostQueue *;

struct HostQueueSet
{
    UBaseType_t event_queue_length = 0;
    std::vector<QueueHandle_t> members;
};

using QueueSetHandle_t = HostQueueSet *;
using QueueSetMemberHandle_t = QueueHandle_t;

inline QueueHandle_t xQueueCreate(size_t length, size_t itemSize)
{
    (void)length;
    auto *queue = new HostQueue{};
    queue->item_size = itemSize;
    return queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticksToWait)
{
    (void)ticksToWait;
    if (queue == nullptr || item == nullptr)
    {
        return pdFAIL;
    }

    std::vector<uint8_t> bytes(queue->item_size);
    std::memcpy(bytes.data(), item, queue->item_size);
    queue->items.push_back(std::move(bytes));
    return pdPASS;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticksToWait)
{
    (void)ticksToWait;
    if (queue == nullptr || item == nullptr || queue->items.empty() || queue->items.front().size() != queue->item_size)
    {
        return pdFAIL;
    }

    std::memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.erase(queue->items.begin());
    return pdPASS;
}

inline QueueSetHandle_t xQueueCreateSet(UBaseType_t eventQueueLength)
{
    auto *queueSet = new HostQueueSet{};
    queueSet->event_queue_length = eventQueueLength;
    return queueSet;
}

inline BaseType_t xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t queueSet)
{
    if (queue == nullptr || queueSet == nullptr)
    {
        return pdFAIL;
    }

    for (QueueHandle_t member : queueSet->members)
    {
        if (member == queue)
        {
            return pdPASS;
        }
    }

    queueSet->members.push_back(queue);
    return pdPASS;
}

inline QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t queueSet, TickType_t ticksToWait)
{
    (void)ticksToWait;
    if (queueSet == nullptr)
    {
        return nullptr;
    }

    for (QueueHandle_t member : queueSet->members)
    {
        if (member != nullptr && !member->items.empty())
        {
            return member;
        }
    }

    return nullptr;
}

inline size_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    if (queue == nullptr)
    {
        return 0;
    }

    return queue->items.size();
}

template <typename T>
inline bool hostQueuePop(QueueHandle_t queue, T &value)
{
    if (queue == nullptr || queue->items.empty() || queue->items.front().size() != sizeof(T))
    {
        return false;
    }

    std::memcpy(&value, queue->items.front().data(), sizeof(T));
    queue->items.erase(queue->items.begin());
    return true;
}

inline void vQueueDelete(QueueHandle_t queue)
{
    delete queue;
}

inline void vQueueDelete(QueueSetHandle_t queueSet)
{
    delete queueSet;
}

#endif // TESTS_HOST_SHIMS_QUEUE_H