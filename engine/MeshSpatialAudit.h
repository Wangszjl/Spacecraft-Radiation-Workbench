#pragma once
#include "PreviewMeshQuality.h"
namespace spacerad::engine {
struct MeshSpatialBudget {
    std::size_t trianglePairs=200000,nodePairs=1000000,containmentTriangles=200000,componentPairs=128,issues=128;
};
// Exact signs for supported binary64 input coordinates; no geometric snapping.
// Every limit or unsupported numeric case is incomplete, never clear.
Json auditMeshSpatial(const TriangleMesh&,const MeshSpatialBudget& = {});
// Explicit v2 traversal: one triangle per leaf. Legacy defaults remain v1.
Json auditMeshSpatialLeafBvh(const TriangleMesh&,const MeshSpatialBudget& = {});
// Algebraic boundary volume only; callers must establish closed, separated
// material boundaries. Determinants are summed before rounding/dividing by 6.
double signedMeshBoundaryVolume(const TriangleMesh&);
struct MeshAssemblyPart { std::string id;TriangleMesh mesh; };
struct MeshAssemblyBudget {
    std::size_t trianglePairs=200000,nodePairs=1000000,containmentTriangles=200000,partPairs=1024,issues=128;
};
// Each mesh is already in world mm. Distinct parts never share a simplex:
// even exact face/edge/vertex contact is an unresolved assembly issue.
Json auditMeshAssembly(const std::vector<MeshAssemblyPart>&,const MeshAssemblyBudget& = {});
Json auditMeshAssemblyLeafBvh(const std::vector<MeshAssemblyPart>&,const MeshAssemblyBudget& = {});
struct MeshContactBudget {
    std::size_t supportVertices=200000,pointTriangles=1000000,identityTriangles=200000;
    std::size_t boundaryTriangles=200000,boundaryNodes=1000000;
};
// Sufficient proofs only, for qualified single closed filled meshes. A proof
// never clears the existing unresolved contact/transport gate.
Json explainMeshAssemblyContacts(const std::vector<MeshAssemblyPart>&,const MeshContactBudget& = {});
}
