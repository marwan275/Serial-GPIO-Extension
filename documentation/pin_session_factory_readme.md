# PinSessionFactory Guide

This document explains the runtime factory pattern implemented in the repository and how to add new pin session types.

## Purpose

`PinSessionFactory` centralizes construction of concrete `PinSession` instances so the dispatcher does not need to know about every derived session type. The factory is pluggable: modules register a factory function for each `PinType` they support.

## API Overview

- `bool PinSessionFactory::registerFactory(PinType pinType, FactoryFunc factory)`
  - Registers a factory callable for `pinType`. Factories are stored in a global registry keyed by the `PinType` byte.

- `std::unique_ptr<PinSession> PinSessionFactory::createFromRequest(const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue)`
  - Lookup and invoke the registered factory for `request.pin_type`. The factory is responsible for allocating the session's request queue and creating the worker task.

- `PinSessionFactory::makeSessionWithResources<SessionT>(...)`
  - A templated helper that creates the per-session `requestQueue`, invokes a small `configBuilder` to fill a `PinModeConfig`, constructs the `SessionT` instance, creates the worker `Task`, and returns a `std::unique_ptr<PinSession>` on success or `nullptr` on failure.

## Factory contract

A factory callable has the signature `std::unique_ptr<PinSession>(const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue)` and must:

1. Validate the request and build a `PinModeConfig` describing the requested session mode.
2. Create the session's `requestQueue` (or let the helper do it).
3. Create the session object (derived from `PinSession`) and assign any initial state.
4. Create the pin worker task and call `session->setWorkerTask(taskHandle)`.
5. Return the created session as a `std::unique_ptr<PinSession>`; on any failure, cleanup any allocated resources and return `nullptr`.

## Example (digital pin)

Modules typically use static registration helpers at file scope:

```cpp
struct DigitalPinFactoryRegistrar {
    DigitalPinFactoryRegistrar() {
        static constexpr UBaseType_t kRequestQueueDepth = 8;
        static constexpr uint16_t kWorkerStackDepth = 256;
        static constexpr char kWorkerName[] = "DigitalPin";

        PinSessionFactory::registerFactory(
            PinType::kDigital,
            [](const Frame::RequestFrame &request, QueueHandle_t globalResponseQueue) -> std::unique_ptr<PinSession> {
                return PinSessionFactory::makeSessionWithResources<DigitalPinSession>(
                    request, globalResponseQueue, kRequestQueueDepth, kWorkerStackDepth, kWorkerName,
                    [](const Frame::RequestFrame &r, PinModeConfig &config) -> bool {
                        // Build config or return false to reject
                        config.pinType = r.pin_type;
                        if (r.type == FrameType::kRead) config.pinMode = PinMode::kInput;
                        else if (r.type == FrameType::kWrite) config.pinMode = PinMode::kOutput;
                        else return false;
                        return true;
                    }
                );
            }
        );
    }
};
static DigitalPinFactoryRegistrar s_digitalPinFactoryRegistrar;
```

## Recommendations for implementers

- Pick small per-pin queue depths (e.g. 4–16) to keep RAM usage reasonable on embedded targets.
- Use `makeSessionWithResources` to reduce duplicated cleanup logic if the helper meets your needs.
- Choose task priorities conservatively; do not create every pin worker at the highest priority.
- Create worker tasks suspended if you want the session to call `startWorkerTask()` at the right moment after initialization.
- Implement a deterministic graceful-shutdown path (sentinel request or task notification) if you expect sessions to be torn down at runtime.

## Where to look in the repo

- `pin_session_factory.h` — helper and API
- `pin_session_factory.cpp` — registry implementation
- `digital_pin.cpp`, `analog_pin.cpp`, `servo_pin.cpp` — example factories and registration
- `request_dispatcher.cpp` — call site where `createFromRequest()` is used

*** End of guide
