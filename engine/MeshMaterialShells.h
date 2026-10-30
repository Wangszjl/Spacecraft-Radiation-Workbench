#pragma once
#include "MeshSpatialAudit.h"
namespace spacerad::engine {
struct MeshMaterialReview { Json quality,spatial,semantics; };
// One solid root with explicitly declared, separated direct cavity children.
// This is a bounded review, never a transport certificate.
MeshMaterialReview auditMeshMaterialShells(const TriangleMesh&,const Json& declarations);
}
