#pragma once
#include "Faction.h"
#include <cstdint>
#include <vector>

namespace engine {

class EventBus;

class TurnManager {
public:
    // factionOrder defines the turn sequence, e.g. {Player, Enemy}.
    // EventBus is stored by pointer; TurnManager publishes TurnStarted/
    // TurnEnded/ActionSpent events on state transitions.
    void init(EventBus& bus, std::vector<Faction> factionOrder, uint8_t actionsPerTurn = 2);

    // Called by game when player/AI finishes their turn
    void endTurn();

    // Called by MovementSystem, CombatSystem before executing an action
    bool canAct()      const;
    bool spendAction();        // returns false if no actions left; auto-ends turn

    Faction  currentFaction() const { return m_factionOrder[m_currentIndex]; }
    uint8_t  actionsLeft()    const { return m_actionsLeft;  }
    uint32_t round()          const { return m_round;        }

    // Convenience: check if a specific faction can act right now
    bool isFactionActive(Faction f) const { return currentFaction() == f; }

private:
    EventBus*            m_bus = nullptr;
    std::vector<Faction> m_factionOrder;
    uint8_t  m_actionsPerTurn = 2;
    uint8_t  m_actionsLeft    = 0;
    uint32_t m_currentIndex   = 0;
    uint32_t m_round          = 1;
};

} // namespace engine
