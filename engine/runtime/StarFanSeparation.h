#pragma once
#include "ConvexSeparation.h"
namespace spacerad::engine {
struct StarFanBudget {
    size_t cellPairs=4096,vertexProjections=1000000,kernelFaces=32768;
};
// Preconditions: embedded, connected, closed, outward oriented source surfaces.
// A strict kernel and oriented boundary cancellation certify a positive tetra
// cover. Cells are audit objects only; transport retains the original solids.
Json auditStarFanSeparation(const std::vector<TriangleMeshReview>&,
    const Json& covers,const StarFanBudget& = {});
Json starFanAssemblyDemo(const std::string& particle="e-");
}
