#pragma once
#include "Gameplay/TurnManager/Faction.h"

namespace engine {

struct FactionComponent {
    Faction faction = Faction::Neutral;

    FactionComponent() = default;
    explicit FactionComponent(Faction faction) : faction(faction) {}
};

} // namespace engine
