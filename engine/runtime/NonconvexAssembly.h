#pragma once
#include "MeshExperimentJob.h"
namespace spacerad::engine {
// Projection containment holds for any bounded triangle solid, including concave
// solids. This proves only separated vertex hulls; no witness is not overlap.
Json auditVertexHullSeparation(const std::vector<TriangleMeshReview>&);
Json nonconvexAssemblyDemo(const std::string& particle="e-");
Json prepareStepMeshAssembly(const QString& manifest,const std::string& particle="e-");
// Exact local fixture recognition for CLI G4ExtrudedSolid/G4Box controls.
std::string nonconvexReferenceProfile(const TriangleMeshReview&);
void validateNonconvexOrigins(const Json& job,const std::vector<TriangleMeshReview>&);
}
