#pragma once
#include "Scene/Systems/System.h"
#include "Core/Events/Subscription.h"

namespace engine {

class EventBus;
class InputMap;
class Swapchain;
class DebugDraw;
class GridMap;

// Tracks hovered + selected tiles and draws a debug highlight for each via
// DebugDraw. Reads mouse position from the polling Input API, converts to
// world coords using the active camera, clamps to the grid.
//
// Registers a SelectionState singleton in registry.ctx() so other gameplay
// systems can query the current selection without needing a pointer to us.
class SelectionSystem final : public System {
public:
    void init(entt::registry& registry,
              EventBus& bus,
              const Swapchain& swapchain,
              const GridMap& grid,
              DebugDraw& debugDraw);
    void update(entt::registry& registry, float realDt) override;
    void shutdown(entt::registry& registry) override;

private:
    const Swapchain* m_swapchain = nullptr;
    const GridMap*   m_grid      = nullptr;
    DebugDraw*       m_debugDraw = nullptr;

    Subscription     m_actionSub;
};

} // namespace engine
