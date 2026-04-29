#include "arduino_freertos.h"
#include "app_tasks.h"

void setup()
{
  Serial.begin(0);
  if (CrashReport)
  {
    Serial.print(CrashReport);
    Serial.println();
    Serial.flush();
  }

  // Create the fixed queues before any task can start using them.
  if (!ApplicationQueues::createQueues())
  {
    while (true)
    {
    }
  }

  if (!ApplicationTasks::createTasks())
  {
    while (true)
    {
    }
  }

  vTaskStartScheduler();
}

void loop() {}
