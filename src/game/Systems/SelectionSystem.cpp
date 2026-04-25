#include "SelectionSystem.h"

#include "Components/SelectionState.h"

#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"

#include "Platform/Input/Input.h"
#include "Platform/Input/InputMap.h"

#include "Renderer/DebugDraw.h"
#include "Renderer/Swapchain.h"

#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"

#include "Gameplay/GridMap/GridMap.h"

#include <entt/entt.hpp>

#include <cmath>

namespace engine {

// Hardcoded tuning — move to a config later if gameplay needs it.
static constexpr glm::vec4 kHoverColor    { 1.00f, 1.00f, 1.00f, 0.70f };
static constexpr glm::vec4 kSelectedColor { 1.00f, 0.90f, 0.20f, 1.00f };
// Small inset so the highlight sits inside the tile edge (avoids z-fighting
// with neighbour highlights and looks less cramped visually).
static constexpr float     kInset         = 0.02f;

static const ActionID kTileSelect("tile.select");
static const ActionID kTileCancel("tile.cancel");

// ── Public ───────────────────────────────────────────────────────────────────

void SelectionSystem::init(entt::registry& registry,
                           EventBus& bus,
                           const Swapchain& swapchain,
                           const GridMap& grid,
                           DebugDraw& debugDraw) {
    m_swapchain = &swapchain;
    m_grid      = &grid;
    m_debugDraw = &debugDraw;

    // Install the per-scene selection singleton.
    registry.ctx().emplace<SelectionState>();

    // Click commits / cancels the hover into the selected slot.
    m_actionSub = bus.subscribe<ActionTriggeredEvent>(
        [&registry](const ActionTriggeredEvent& e) {
            auto& state = registry.ctx().get<SelectionState>();
            if (e.action == kTileSelect) {
                state.selected = state.hovered;  // nullopt if hovering nothing
            } else if (e.action == kTileCancel) {
                state.selected.reset();
            }
        });

    FP_CORE_INFO("SelectionSystem initialized");
}

void SelectionSystem::shutdown(entt::registry& registry) {
    m_actionSub.release();
    registry.ctx().erase<SelectionState>();
    m_swapchain = nullptr;
    m_grid      = nullptr;
    m_debugDraw = nullptr;
}

void SelectionSystem::update(entt::registry& registry, float /*realDt*/) {
    if (!m_swapchain || !m_grid || !m_debugDraw) return;

    // Resolve the active camera.
    auto camView = registry.view<const ActiveCameraTag,
                                 const TransformComponent,
                                 const CameraComponent2D>();
    const TransformComponent* tc = nullptr;
    const CameraComponent2D*  cc = nullptr;
    for (auto e : camView) {
        tc = &camView.get<const TransformComponent>(e);
        cc = &camView.get<const CameraComponent2D>(e);
        break;
    }
    if (!tc || !cc) return;

    // Mouse → world → grid cell.
    const glm::vec2 mousePx{ static_cast<float>(Input::getMouseX()),
                             static_cast<float>(Input::getMouseY()) };
    const glm::vec2 world = screenToWorld(mousePx, *tc, *cc,
                                          m_swapchain->extent().width,
                                          m_swapchain->extent().height);
    const int32_t col = static_cast<int32_t>(std::floor(world.x));
    const int32_t row = static_cast<int32_t>(std::floor(world.y));

    auto& state = registry.ctx().get<SelectionState>();
    state.hovered = m_grid->inBounds(col, row)
        ? std::optional<glm::ivec2>{ { col, row } }
        : std::nullopt;

    // ── Draw highlights via DebugDraw ────────────────────────────────────
    if (state.hovered) {
        const auto c = *state.hovered;
        m_debugDraw->box({ float(c.x) + kInset,     float(c.y) + kInset },
                         { float(c.x) + 1 - kInset, float(c.y) + 1 - kInset },
                         kHoverColor);
    }
    if (state.selected) {
        const auto c = *state.selected;
        m_debugDraw->box({ float(c.x) + kInset,     float(c.y) + kInset },
                         { float(c.x) + 1 - kInset, float(c.y) + 1 - kInset },
                         kSelectedColor);
    }
}

} // namespace engine
