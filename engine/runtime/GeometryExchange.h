#pragma once
#include "RadiationProject.h"
#include <QString>
namespace spacerad::engine {
// Canonical geometry, instances and material definitions. No environment,
// exposure, source sphere, seed, physics settings or result fields.
Json geometryDescriptor(const Project& project);
std::string geometryDescriptorSha256(const Project& project);
Json exportGeometryBundle(const Project& project,const Json& sourceIdentity);
Project applyGeometryBundle(const Project& base,const Json& bundle,const Json& bundleIdentity);
Project importGeometryBundleFile(const Project& base,const QString& path);
void exportGeometryBundleFile(const Project& project,const QString& path,const Json& sourceIdentity);
Json geometryBundleReport(const Project& base,const QString& path);
}
