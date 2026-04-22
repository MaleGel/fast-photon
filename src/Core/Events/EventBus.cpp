#include "EventBus.h"

namespace engine {

void EventBus::unsubscribe(std::type_index type, uint64_t id) {
    auto it = m_handlers.find(type);
    if (it == m_handlers.end()) return;

    auto& slots = it->second;
    for (auto slot = slots.begin(); slot != slots.end(); ++slot) {
        if (slot->id == id) {
            slots.erase(slot);
            return;
        }
    }
}

// ── Subscription ─────────────────────────────────────────────────────────────

Subscription::~Subscription() {
    release();
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_bus(other.m_bus), m_type(other.m_type), m_id(other.m_id) {
    other.m_bus = nullptr;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        release();
        m_bus       = other.m_bus;
        m_type      = other.m_type;
        m_id        = other.m_id;
        other.m_bus = nullptr;
    }
    return *this;
}

void Subscription::release() {
    if (m_bus) {
        m_bus->unsubscribe(m_type, m_id);
        m_bus = nullptr;
    }
}

} // namespace engine
