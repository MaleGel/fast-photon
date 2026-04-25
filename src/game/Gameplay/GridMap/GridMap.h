#pragma once
#include "Tile.h"
#include <vector>
#include <cstdint>
#include <optional>

namespace engine {

class GridMap {
public:
    GridMap() = default;
    GridMap(int32_t width, int32_t height, Tile defaultTile = {});

    // Tile access
    Tile&       at(int32_t col, int32_t row);
    const Tile& at(int32_t col, int32_t row) const;

    // Bounds & validation
    bool inBounds(int32_t col, int32_t row) const;
    bool isPassable(int32_t col, int32_t row) const;

    // Occupancy
    bool         isOccupied(int32_t col, int32_t row) const;
    void         setOccupant(int32_t col, int32_t row, entt::entity entity);
    void         clearOccupant(int32_t col, int32_t row);
    entt::entity getOccupant(int32_t col, int32_t row) const;

    // Neighbours (4-directional: up/down/left/right)
    std::vector<std::pair<int32_t,int32_t>> getNeighbours(int32_t col, int32_t row) const;

    int32_t width()  const { return m_width;  }
    int32_t height() const { return m_height; }

private:
    int32_t        m_width  = 0;
    int32_t        m_height = 0;
    std::vector<Tile> m_tiles;

    int32_t index(int32_t col, int32_t row) const { return col + row * m_width; }
};

} // namespace engine
