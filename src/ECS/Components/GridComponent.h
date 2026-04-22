#pragma once
#include <cstdint>

namespace engine {

struct GridComponent {
    int32_t col = 0;  // X axis on the grid
    int32_t row = 0;  // Y axis on the grid

    GridComponent() = default;
    GridComponent(int32_t col, int32_t row) : col(col), row(row) {}

    bool operator==(const GridComponent& o) const { return col == o.col && row == o.row; }
    bool operator!=(const GridComponent& o) const { return !(*this == o); }
};

} // namespace engine
