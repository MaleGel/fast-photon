#pragma once
#include "Subscription.h"

#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace engine {

// Synchronous, statically-typed, single-threaded event bus.
//
//     struct FooEvent { int x; };
//
//     EventBus bus;
//     auto sub = bus.subscribe<FooEvent>([](const FooEvent& e) { ... });
//     bus.publish<FooEvent>({ 42 });      // handler fires here, synchronously
//     // 'sub' going out of scope removes the handler.
//
// Contract: all Subscriptions must be released before the bus is destroyed.
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Register a handler for events of type E. Returns a RAII token.
    template<typename E>
    [[nodiscard]] Subscription subscribe(std::function<void(const E&)> handler) {
        const std::type_index type = std::type_index(typeid(E));
        const uint64_t id = ++m_lastId;

        // Wrap the typed handler in a type-erased trampoline. The cast is safe
        // because only publish<E> can reach this slot — type_index is the key.
        auto slot = HandlerSlot{
            id,
            [fn = std::move(handler)](const void* raw) {
                fn(*static_cast<const E*>(raw));
            }
        };

        m_handlers[type].emplace_back(std::move(slot));
        return Subscription(this, type, id);
    }

    // Invoke every handler subscribed to E with 'event'.
    template<typename E>
    void publish(const E& event) {
        const std::type_index type = std::type_index(typeid(E));
        auto it = m_handlers.find(type);
        if (it == m_handlers.end()) return;

        // Copy the vector so handlers can subscribe/unsubscribe during dispatch
        // without invalidating our iteration.
        auto handlers = it->second;
        for (auto& slot : handlers) {
            slot.invoke(&event);
        }
    }

    // Called by ~Subscription() — do not invoke directly.
    void unsubscribe(std::type_index type, uint64_t id);

private:
    struct HandlerSlot {
        uint64_t id;
        std::function<void(const void*)> invoke;
    };

    std::unordered_map<std::type_index, std::vector<HandlerSlot>> m_handlers;
    uint64_t m_lastId = 0;
};

} // namespace engine
