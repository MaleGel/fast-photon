#include "TurnManager.h"
#include "TurnEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

namespace engine {

void TurnManager::init(EventBus& bus, std::vector<Faction> factionOrder, uint8_t actionsPerTurn) {
    FP_CORE_ASSERT(!factionOrder.empty(), "TurnManager: factionOrder must not be empty");

    m_bus             = &bus;
    m_factionOrder    = std::move(factionOrder);
    m_actionsPerTurn  = actionsPerTurn;
    m_actionsLeft     = actionsPerTurn;
    m_currentIndex    = 0;
    m_round           = 1;

    FP_CORE_INFO("TurnManager initialized — round {}, faction {}, {} actions/turn",
        m_round, static_cast<int>(currentFaction()), m_actionsPerTurn);

    // Announce the opening turn so listeners can initialise their per-turn state.
    m_bus->publish(TurnStartedEvent{ currentFaction(), m_round, m_actionsLeft });
}

void TurnManager::endTurn() {
    const Faction  ended  = currentFaction();
    const uint32_t index  = (m_currentIndex + 1) % static_cast<uint32_t>(m_factionOrder.size());
    const Faction  next   = m_factionOrder[index];

    m_currentIndex = index;
    if (m_currentIndex == 0)
        ++m_round;
    m_actionsLeft = m_actionsPerTurn;

    FP_CORE_INFO("Turn ended — round {}, now faction {} acts ({} actions)",
        m_round, static_cast<int>(currentFaction()), m_actionsLeft);

    if (m_bus) {
        m_bus->publish(TurnEndedEvent  { ended, next, m_round });
        m_bus->publish(TurnStartedEvent{ next,  m_round, m_actionsLeft });
    }
}

bool TurnManager::canAct() const {
    return m_actionsLeft > 0;
}

bool TurnManager::spendAction() {
    if (m_actionsLeft == 0)
        return false;

    --m_actionsLeft;
    FP_CORE_TRACE("Action spent — {} remaining", m_actionsLeft);

    if (m_bus)
        m_bus->publish(ActionSpentEvent{ currentFaction(), m_actionsLeft });

    if (m_actionsLeft == 0)
        endTurn();

    return true;
}

} // namespace engine
