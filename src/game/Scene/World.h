#pragma once
#include "Gameplay/GridMap/GridMap.h"
#include "Gameplay/TurnManager/TurnManager.h"
#include "Systems/RenderSystem.h"
#include "Systems/CameraController.h"
#include "Scene/PrefabRegistry.h"

#include <entt/entt.hpp>

namespace engine {

class EventBus;
class InputMap;

// Owner of gameplay simulation state: ECS registry, GridMap, TurnManager,
// and all Systems that operate on them. Rendering is NOT part of World —
// GridRenderer borrows world data via the getters below when it draws.
//
// Not owned here (held by reference / pointer): EventBus, InputMap.
// These are engine-wide and outlive any single scene.
class World {
public:
    void init(EventBus& bus, InputMap& input);
    void shutdown();

    // Fixed-timestep simulation step. May fire 0..N times per frame.
    void fixedUpdate(float fixedDt);

    // Presentation-domain update. Fires once per frame.
    void update(float realDt);

    // ── Accessors (used by scene loader + renderer) ────────────────────
    entt::registry&       registry()        { return m_registry; }
    GridMap&              grid()            { return m_grid; }
    TurnManager&          turnManager()     { return m_turnManager; }
    PrefabRegistry&       prefabs()         { return m_prefabs; }

    const entt::registry& registry()    const { return m_registry; }
    const GridMap&        grid()        const { return m_grid; }
    const TurnManager&    turnManager() const { return m_turnManager; }
    const PrefabRegistry& prefabs()     const { return m_prefabs; }

private:
    // Data
    entt::registry   m_registry;
    GridMap          m_grid;
    TurnManager      m_turnManager;
    PrefabRegistry   m_prefabs;

    // Systems
    RenderSystem     m_renderSystem;
    CameraController m_cameraController;

    // References (not owned)
    EventBus*        m_bus   = nullptr;
    InputMap*        m_input = nullptr;
};

} // namespace engine
