#include "pin_session_factory.h"
#include <unordered_map>
#include <functional>
#include <cstdint>

using RegistryKey = uint8_t;

namespace
{
    static std::unordered_map<RegistryKey, PinSessionFactory::FactoryFunc> &getRegistry()
    {
        static std::unordered_map<RegistryKey, PinSessionFactory::FactoryFunc> registry;
        return registry;
    }
}

bool PinSessionFactory::registerFactory(PinType pinType, FactoryFunc factory)
{
    auto &registry = getRegistry();
    registry[static_cast<RegistryKey>(pinType)] = std::move(factory);
    return true;
}

std::unique_ptr<PinSession> PinSessionFactory::createFromRequest(const Frame::RequestFrame &request,
                                                                 QueueHandle_t globalResponseQueue)
{
    auto &registry = getRegistry();
    auto it = registry.find(static_cast<RegistryKey>(request.pin_type));
    if (it == registry.end())
    {
        return nullptr;
    }

    return it->second(request, globalResponseQueue);
}

