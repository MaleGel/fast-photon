#include "JsonSceneLoader.h"
#include "World.h"
#include "Scene/PrefabRegistry.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

// Engine-side generic components.
#include "Scene/Components/TagComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/SpriteComponent.h"
#include "Scene/Components/AnimationComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraConfig.h"

// Game-side components.
#include "Components/GridComponent.h"
#include "Components/RenderComponent.h"
#include "Components/FactionComponent.h"

#include "Gameplay/GridMap/GridMap.h"
#include "Gameplay/TurnManager/TurnManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

namespace engine {

using json = nlohmann::json;

// ── Enum string parsers ──────────────────────────────────────────────────────

static TileType parseTileType(const std::string& s) {
    if (s == "grass")    return TileType::Grass;
    if (s == "forest")   return TileType::Forest;
    if (s == "mountain") return TileType::Mountain;
    if (s == "water")    return TileType::Water;
    FP_CORE_ASSERT(false, "Unknown tile type '{}'", s);
    return TileType::Grass;
}

static Faction parseFaction(const std::string& s) {
    if (s == "Player")  return Faction::Player;
    if (s == "Enemy")   return Faction::Enemy;
    if (s == "Neutral") return Faction::Neutral;
    FP_CORE_ASSERT(false, "Unknown faction '{}'", s);
    return Faction::Neutral;
}

// ── Section parsers ──────────────────────────────────────────────────────────

static void loadGrid(World& world, const json& j) {
    GridMap& grid = world.grid();
    grid = GridMap(j.at("width").get<int32_t>(), j.at("height").get<int32_t>());

    if (!j.contains("overrides")) return;

    for (const auto& o : j.at("overrides")) {
        const int32_t col = o.at("col").get<int32_t>();
        const int32_t row = o.at("row").get<int32_t>();
        FP_CORE_ASSERT(grid.inBounds(col, row),
                       "Grid override out of bounds: ({}, {})", col, row);

        Tile& tile = grid.at(col, row);
        if (o.contains("type"))         tile.type         = parseTileType(o.at("type").get<std::string>());
        if (o.contains("passable"))     tile.passable     = o.at("passable").get<bool>();
        if (o.contains("movementCost")) tile.movementCost = o.at("movementCost").get<uint8_t>();
    }
}

static void loadTurn(World& world, EventBus& bus, const json& j) {
    std::vector<Faction> order;
    for (const auto& f : j.at("factionOrder")) {
        order.push_back(parseFaction(f.get<std::string>()));
    }
    const uint8_t actionsPerTurn = j.value("actionsPerTurn", 2);
    world.turnManager().init(bus, std::move(order), actionsPerTurn);
}

static void loadCamera(World& world, const json& j, float aspectRatio) {
    const CameraConfig camCfg = loadCameraConfig("assets/data/camera_config.json");

    glm::vec3 pos(0.f, 0.f, 0.f);
    if (j.contains("position")) {
        const auto& p = j.at("position");
        pos.x = p.at(0).get<float>();
        pos.y = p.at(1).get<float>();
    }

    auto& registry = world.registry();
    auto camera = registry.create();
    registry.emplace<TagComponent>(camera, "Camera");
    registry.emplace<TransformComponent>(camera, pos);
    registry.emplace<CameraComponent2D>(camera, CameraComponent2D{
        camCfg.zoom, aspectRatio, camCfg.nearPlane, camCfg.farPlane,
        camCfg.panSpeed, camCfg.zoomStep, camCfg.zoomMin, camCfg.zoomMax,
    });
    registry.emplace<ActiveCameraTag>(camera);
}

// ── Component parsers ────────────────────────────────────────────────────────

static void emplaceTag(entt::registry& r, entt::entity e, const json& v) {
    // emplace_or_replace so prefab → override works naturally.
    r.emplace_or_replace<TagComponent>(e, v.get<std::string>());
}

static void emplaceTransform(entt::registry& r, entt::entity e, const json& v) {
    const auto& p = v.at("pos");
    r.emplace_or_replace<TransformComponent>(e, glm::vec3(
        p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>()));
}

static void emplaceGrid(entt::registry& r, entt::entity e, const json& v) {
    r.emplace_or_replace<GridComponent>(e,
        v.at("col").get<int32_t>(),
        v.at("row").get<int32_t>());
}

static void emplaceRender(entt::registry& r, entt::entity e, const json& v) {
    const auto& c = v.at("color");
    FP_CORE_ASSERT(c.size() == 4,
                   "Render.color must have exactly 4 elements (got {})", c.size());
    r.emplace_or_replace<RenderComponent>(e, glm::vec4(
        c.at(0).get<float>(), c.at(1).get<float>(),
        c.at(2).get<float>(), c.at(3).get<float>()));
}

static void emplaceFaction(entt::registry& r, entt::entity e, const json& v) {
    r.emplace_or_replace<FactionComponent>(e, parseFaction(v.get<std::string>()));
}

static void emplaceSprite(entt::registry& r, entt::entity e, const json& v) {
    SpriteComponent sc;
    sc.sprite = SpriteID(v.at("id").get<std::string>().c_str());
    if (v.contains("tint")) {
        const auto& c = v.at("tint");
        FP_CORE_ASSERT(c.size() == 4,
                       "Sprite.tint must have exactly 4 elements (got {})", c.size());
        sc.tint = { c.at(0).get<float>(), c.at(1).get<float>(),
                    c.at(2).get<float>(), c.at(3).get<float>() };
    }
    if (v.contains("size")) {
        const auto& s = v.at("size");
        FP_CORE_ASSERT(s.size() == 2, "Sprite.size must have 2 elements");
        sc.size = { s.at(0).get<float>(), s.at(1).get<float>() };
    }
    sc.visible = v.value("visible", true);
    r.emplace_or_replace<SpriteComponent>(e, sc);
}

static void emplaceAnimation(entt::registry& r, entt::entity e, const json& v) {
    AnimationComponent ac;
    ac.set = AnimationSetID(v.at("set").get<std::string>().c_str());
    // 'state' is optional — when absent, AnimationSystem falls back to the
    // set's default state on its first tick.
    if (v.contains("state")) {
        ac.state = StateID(v.at("state").get<std::string>().c_str());
    }
    ac.paused = v.value("paused", false);
    r.emplace_or_replace<AnimationComponent>(e, ac);
}

static void emplaceComponent(entt::registry& r, entt::entity e,
                             const std::string& key, const json& v) {
    if      (key == "Tag")       emplaceTag      (r, e, v);
    else if (key == "Transform") emplaceTransform(r, e, v);
    else if (key == "Grid")      emplaceGrid     (r, e, v);
    else if (key == "Render")    emplaceRender   (r, e, v);
    else if (key == "Faction")   emplaceFaction  (r, e, v);
    else if (key == "Sprite")    emplaceSprite   (r, e, v);
    else if (key == "Animation") emplaceAnimation(r, e, v);
    else {
        FP_CORE_ASSERT(false, "Unknown component '{}' in scene JSON", key);
    }
}

// Merge source object into target (shallow — top-level keys replace).
// Used to apply 'overrides' on top of prefab components.
static void shallowMerge(json& target, const json& source) {
    for (auto it = source.begin(); it != source.end(); ++it) {
        target[it.key()] = it.value();
    }
}

static void loadEntities(World& world, const PrefabRegistry& prefabs,
                         const json& j) {
    auto& registry = world.registry();
    auto& grid     = world.grid();

    for (const auto& entityJson : j) {
        auto entity = registry.create();

        // Step 1: assemble the final component set, starting from the prefab
        // (if any), then merging overrides / inline components on top.
        json components = json::object();

        if (entityJson.contains("prefab")) {
            const std::string pid = entityJson.at("prefab").get<std::string>();
            const Prefab* prefab = prefabs.get(PrefabID(pid.c_str()));
            FP_CORE_ASSERT(prefab != nullptr, "Prefab '{}' not found", pid);
            components = prefab->components;
        }

        if (entityJson.contains("overrides")) {
            shallowMerge(components, entityJson.at("overrides"));
        }
        if (entityJson.contains("components")) {
            shallowMerge(components, entityJson.at("components"));
        }

        // Step 2: if the entity is grid-placed, synthesise Grid + Transform
        // (center of the cell). These fields aren't meant to live in the prefab.
        if (entityJson.contains("grid")) {
            const int32_t col = entityJson.at("grid").at("col").get<int32_t>();
            const int32_t row = entityJson.at("grid").at("row").get<int32_t>();
            components["Grid"] = { {"col", col}, {"row", row} };
            components["Transform"] = {
                {"pos", { static_cast<float>(col) + 0.5f,
                          static_cast<float>(row) + 0.5f,
                          0.0f }}
            };
        }

        // Step 3: actually emplace the components.
        for (auto it = components.begin(); it != components.end(); ++it) {
            emplaceComponent(registry, entity, it.key(), it.value());
        }

        // Step 4: register grid occupancy if the entity is on the map.
        if (auto* g = registry.try_get<GridComponent>(entity)) {
            grid.setOccupant(g->col, g->row, entity);
        }
    }
}

// ── Public ───────────────────────────────────────────────────────────────────

void JsonSceneLoader::load(World& world, EventBus& bus,
                           const PrefabRegistry& prefabs,
                           const std::string& path, float aspectRatio) {
    std::ifstream file(path);
    FP_CORE_ASSERT(file.is_open(), "Cannot open scene file: {}", path);
    json doc = json::parse(file);

    loadGrid   (world, doc.at("grid"));
    loadTurn   (world, bus, doc.at("turn"));
    loadCamera (world, doc.at("camera"), aspectRatio);
    loadEntities(world, prefabs, doc.at("entities"));

    FP_CORE_INFO("Scene loaded: {} ({} entities, {}x{} grid)",
                 path,
                 world.registry().storage<entt::entity>().size(),
                 world.grid().width(), world.grid().height());
}

} // namespace engine
