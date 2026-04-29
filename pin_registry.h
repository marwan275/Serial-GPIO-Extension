#ifndef PIN_REGISTRY_H
#define PIN_REGISTRY_H

#include "pin_session.h"
#include <array>
#include <memory>

namespace PinRegistry
{
    static constexpr uint8_t kMinPhysicalPinNumber = 1;
    static constexpr uint8_t kMaxPhysicalPinNumber = 41;
    static constexpr size_t kPinRegistrySize = kMaxPhysicalPinNumber + 1; // Index 0 is unused; valid pins are 1..41.

    inline std::array<std::unique_ptr<PinSession>, kPinRegistrySize> pinSessions{};

    bool isValidPinNumber(uint8_t pinNumber)
    {
        return pinNumber >= kMinPhysicalPinNumber && pinNumber <= kMaxPhysicalPinNumber;
    }

    PinSession *findSession(uint8_t pinNumber)
    {
        if (!PinRegistry::isValidPinNumber(pinNumber))
        {
            return nullptr;
        }

        return PinRegistry::pinSessions[pinNumber].get();
    }
} // namespace PinRegistry

#endif // !PIN_REGISTRY_H