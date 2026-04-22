#pragma once
#include <cstdint>
#include <typeindex>

namespace engine {

class EventBus;

// RAII handle for a subscription.
// Destroying the Subscription removes the handler from its EventBus.
// Non-copyable, movable. Must not outlive its EventBus.
class Subscription {
public:
    Subscription() = default;
    Subscription(EventBus* bus, std::type_index type, uint64_t id)
        : m_bus(bus), m_type(type), m_id(id) {}

    ~Subscription();

    Subscription(const Subscription&)            = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    // Drop the handler early, before the destructor runs.
    void release();

    bool valid() const { return m_bus != nullptr; }

private:
    EventBus*       m_bus  = nullptr;
    std::type_index m_type = std::type_index(typeid(void));
    uint64_t        m_id   = 0;
};

} // namespace engine
