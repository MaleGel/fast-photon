#include "World.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Core/Profiler/Profiler.h"
#include "Platform/Input/InputMap.h"

namespace engine {

void World::init(EventBus& bus, InputMap& input) {
    m_bus   = &bus;
    m_input = &input;

    m_renderSystem.init(m_registry);
    m_cameraController.init(m_registry, *m_input, *m_bus);

    FP_CORE_INFO("World initialized");
}

void World::shutdown() {
    m_cameraController.shutdown(m_registry);
    m_renderSystem.shutdown(m_registry);
    m_registry.clear();

    m_bus   = nullptr;
    m_input = nullptr;
    FP_CORE_TRACE("World destroyed");
}

void World::fixedUpdate(float fixedDt) {
    FP_PROFILE_SCOPE("World::fixedUpdate");
    // No simulation systems yet. Movement/AI will hook in here.
    (void)fixedDt;
}

void World::update(float realDt) {
    FP_PROFILE_SCOPE("World::update");
    // Camera input must land before render consumes transforms.
    {
        FP_PROFILE_SCOPE("CameraController");
        m_cameraController.update(m_registry, realDt);
    }
    {
        FP_PROFILE_SCOPE("RenderSystem");
        m_renderSystem.update(m_registry, realDt);
    }
}

} // namespace engine
