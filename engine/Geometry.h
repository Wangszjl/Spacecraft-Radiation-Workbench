#pragma once
#include "Instances.h"
namespace spacerad::engine {
using Vector3 = std::array<double,3>;
using Matrix3 = std::array<Vector3,3>;
struct Transform {
    Matrix3 rotation{{{1,0,0},{0,1,0},{0,0,1}}};
    Vector3 translation{};
    Vector3 apply(const Vector3& local) const;
};
struct ResolvedVolume {
    size_t index;
    Transform transform;
    bool visible;
};
struct MaterialBox { Vector3 center, size; };
struct CompiledGeometry:GeometryModel { std::vector<ResolvedVolume> placements; };
CompiledGeometry compileGeometry(const Project& project);
// All consumers use this active, local-to-world transform. Assemblies are
// coordinate groups, not physical mothers; their children remain world siblings.
std::vector<ResolvedVolume> resolveGeometry(const Project& project);
std::vector<MaterialBox> materialBoxes(const Volume& volume);
void validateVolumeShape(const Volume& volume);
double materialVolume(const Volume& volume);
double surfaceArea(const Volume& volume);
std::array<double,6> worldBounds(const Volume& volume,const Transform& transform);
double outerRadiusFromOrigin(const Volume& volume,const Transform& transform);
bool hasAnalyticGeometry(const Project& project);
void validateGeometry(const Project& project);
// Geometry-only diagnostics: no transport approval, CAD mesh certification or
// NIST catalogue lookup. A bounded scan may return incomplete, never valid.
Json geometryDiagnostics(const Project& project,size_t maximumIssues=256,size_t maximumPairs=10000);
bool hasAssemblyGeometry(const Project& project);
Json geometrySnapshot(const Project& project);
}
