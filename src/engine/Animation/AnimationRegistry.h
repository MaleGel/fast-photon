#pragma once
#include "Animation.h"
#include "Renderer/ResourceTypes.h"

#include <unordered_map>

namespace engine {

// Owns Animation tracks and AnimationSet (state machine) descriptions.
// Lookup-only at runtime; population happens once at manifest load.
class AnimationRegistry {
public:
    void addAnimation(AnimationID id, Animation anim);
    void addSet      (AnimationSetID id, AnimationSet set);

    // Returns nullptr if not found.
    const Animation*    getAnimation(AnimationID id) const;
    const AnimationSet* getSet      (AnimationSetID id) const;

    void clear();

private:
    std::unordered_map<AnimationID,    Animation>    m_animations;
    std::unordered_map<AnimationSetID, AnimationSet> m_sets;
};

} // namespace engine
