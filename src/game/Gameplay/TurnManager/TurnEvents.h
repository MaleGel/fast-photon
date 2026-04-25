#pragma once
#include "Faction.h"
#include <cstdint>

namespace engine {

// Fired after TurnManager switches to a new faction.
struct TurnStartedEvent {
    Faction  faction;
    uint32_t round;
    uint32_t actionsLeft;
};

// Fired when a faction finishes its turn (via endTurn or running out of actions).
struct TurnEndedEvent {
    Faction  endedFaction;
    Faction  nextFaction;
    uint32_t round;
};

// Fired when a faction spends one action point.
struct ActionSpentEvent {
    Faction  faction;
    uint32_t actionsLeft;
};

} // namespace engine
