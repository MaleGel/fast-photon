#include "GridMap.h"
#include "Core/Assert/Assert.h"
#include "Core/Log/Log.h"

namespace engine {

GridMap::GridMap(int32_t width, int32_t height, Tile defaultTile)
    : m_width(width), m_height(height), m_tiles(width * height, defaultTile)
{
    FP_CORE_ASSERT(width > 0 && height > 0, "GridMap dimensions must be positive ({} x {})", width, height);
    FP_CORE_INFO("GridMap created ({} x {} = {} tiles)", width, height, width * height);
}

Tile& GridMap::at(int32_t col, int32_t row) {
    FP_CORE_ASSERT(inBounds(col, row), "GridMap::at out of bounds ({}, {})", col, row);
    return m_tiles[index(col, row)];
}

const Tile& GridMap::at(int32_t col, int32_t row) const {
    FP_CORE_ASSERT(inBounds(col, row), "GridMap::at out of bounds ({}, {})", col, row);
    return m_tiles[index(col, row)];
}

bool GridMap::inBounds(int32_t col, int32_t row) const {
    return col >= 0 && col < m_width && row >= 0 && row < m_height;
}

bool GridMap::isPassable(int32_t col, int32_t row) const {
    return inBounds(col, row) && m_tiles[index(col, row)].passable;
}

bool GridMap::isOccupied(int32_t col, int32_t row) const {
    return inBounds(col, row) && m_tiles[index(col, row)].occupant != entt::null;
}

void GridMap::setOccupant(int32_t col, int32_t row, entt::entity entity) {
    FP_CORE_ASSERT(inBounds(col, row), "GridMap::setOccupant out of bounds ({}, {})", col, row);
    m_tiles[index(col, row)].occupant = entity;
}

void GridMap::clearOccupant(int32_t col, int32_t row) {
    FP_CORE_ASSERT(inBounds(col, row), "GridMap::clearOccupant out of bounds ({}, {})", col, row);
    m_tiles[index(col, row)].occupant = entt::null;
}

entt::entity GridMap::getOccupant(int32_t col, int32_t row) const {
    FP_CORE_ASSERT(inBounds(col, row), "GridMap::getOccupant out of bounds ({}, {})", col, row);
    return m_tiles[index(col, row)].occupant;
}

std::vector<std::pair<int32_t,int32_t>> GridMap::getNeighbours(int32_t col, int32_t row) const {
    // Cardinal directions only — standard for turn-based grid movement
    static constexpr std::pair<int32_t,int32_t> dirs[4] = {
        { 0, -1}, { 0,  1},   // up, down
        {-1,  0}, { 1,  0},   // left, right
    };

    std::vector<std::pair<int32_t,int32_t>> result;
    result.reserve(4);

    for (auto [dc, dr] : dirs) {
        int32_t nc = col + dc;
        int32_t nr = row + dr;
        if (inBounds(nc, nr))
            result.push_back({nc, nr});
    }
    return result;
}

} // namespace engine
