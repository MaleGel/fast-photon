#pragma once
#include <string>

namespace engine {

struct TagComponent {
    std::string tag;

    TagComponent() = default;
    explicit TagComponent(std::string tag) : tag(std::move(tag)) {}
};

} // namespace engine
