#pragma once
#include "Geometry.h"
#include <cstddef>
#include <vector>
namespace spacerad::engine {
struct TriangleMesh {
    std::vector<Vector3> points;
    std::vector<std::array<std::size_t,3>> triangles;
};
// Local-mm coordinates, exact-coordinate welding for analysis only. Does not
// repair the mesh or determine material nesting/self-intersections.
Json auditTriangleMesh(const TriangleMesh&);
}
