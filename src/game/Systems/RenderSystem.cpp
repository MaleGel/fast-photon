#include "RenderSystem.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/TagComponent.h"
#include "Components/RenderComponent.h"

#include <imgui.h>

namespace engine {

void RenderSystem::update(entt::registry& registry, float dt) {
    (void)dt;

    if (!ImGui::Begin("RenderSystem")) {
        ImGui::End();
        return;
    }

    ImGui::Text("Visible entities:");
    ImGui::Separator();

    auto view = registry.view<TransformComponent, RenderComponent>();
    for (auto [entity, transform, render] : view.each()) {
        if (!render.visible) continue;

        // Show tag if available, otherwise show raw entity id
        const char* name = "unnamed";
        if (auto* tag = registry.try_get<TagComponent>(entity))
            name = tag->tag.c_str();

        ImGui::TextColored(
            { render.color.r, render.color.g, render.color.b, render.color.a },
            "[%u] %s  pos=(%.1f, %.1f, %.1f)",
            static_cast<uint32_t>(entity), name,
            transform.position.x, transform.position.y, transform.position.z
        );
    }

    ImGui::End();
}

} // namespace engine
