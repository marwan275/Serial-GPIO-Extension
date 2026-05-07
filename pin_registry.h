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
    static constexpr uint8_t kMinI2cAddress = 1;
    static constexpr uint8_t kMaxI2cAddress = 0x7F;
    static constexpr size_t kI2cRegistrySize = kMaxI2cAddress + 1; // Index 0 is unused; valid addresses are 1..127.

    inline std::array<std::unique_ptr<PinSession>, kPinRegistrySize> pinSessions{};
    inline std::array<std::unique_ptr<PinSession>, kI2cRegistrySize> i2cSessions{};

    inline bool isI2cPinType(PinType pinType)
    {
        switch (pinType)
        {
        case PinType::kMCP4725:
            return true;
        default:
            return false;
        }
    }

    inline bool isValidPinNumber(uint8_t pinNumber)
    {
        return pinNumber >= kMinPhysicalPinNumber && pinNumber <= kMaxPhysicalPinNumber;
    }

    inline bool isValidI2cAddress(uint8_t address)
    {
        return address >= kMinI2cAddress && address <= kMaxI2cAddress;
    }

    inline bool isValidPinNumber(PinType pinType, uint8_t pinNumber)
    {
        if (isI2cPinType(pinType))
        {
            return isValidI2cAddress(pinNumber);
        }

        return PinRegistry::isValidPinNumber(pinNumber);
    }

    inline PinSession *findSession(PinType pinType, uint8_t pinNumber)
    {
        if (!PinRegistry::isValidPinNumber(pinType, pinNumber))
        {
            return nullptr;
        }

        if (isI2cPinType(pinType))
        {
            return PinRegistry::i2cSessions[pinNumber].get();
        }

        return PinRegistry::pinSessions[pinNumber].get();
    }

    inline void storeSession(PinType pinType, uint8_t pinNumber, std::unique_ptr<PinSession> session)
    {
        if (!PinRegistry::isValidPinNumber(pinType, pinNumber))
        {
            return;
        }

        if (isI2cPinType(pinType))
        {
            PinRegistry::i2cSessions[pinNumber] = std::move(session);
            return;
        }

        PinRegistry::pinSessions[pinNumber] = std::move(session);
    }
} // namespace PinRegistry

#endif // !PIN_REGISTRY_H