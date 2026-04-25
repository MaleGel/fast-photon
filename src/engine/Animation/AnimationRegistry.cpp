#include "AnimationRegistry.h"
#include "Core/Log/Log.h"

namespace engine {

void AnimationRegistry::addAnimation(AnimationID id, Animation anim) {
    m_animations[id] = std::move(anim);
    FP_CORE_TRACE("Animation registered: '{}'", id.c_str());
}

void AnimationRegistry::addSet(AnimationSetID id, AnimationSet set) {
    m_sets[id] = std::move(set);
    FP_CORE_TRACE("AnimationSet registered: '{}'", id.c_str());
}

const Animation* AnimationRegistry::getAnimation(AnimationID id) const {
    auto it = m_animations.find(id);
    return it == m_animations.end() ? nullptr : &it->second;
}

const AnimationSet* AnimationRegistry::getSet(AnimationSetID id) const {
    auto it = m_sets.find(id);
    return it == m_sets.end() ? nullptr : &it->second;
}

void AnimationRegistry::clear() {
    m_animations.clear();
    m_sets.clear();
}

} // namespace engine
