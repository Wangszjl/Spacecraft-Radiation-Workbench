#pragma once
#include "RadiationProject.h"
namespace spacerad::engine::transport {
Json currentMaterialSnapshot(const Project& project);
Json nistMaterialNames();
// Returns definitions, mappings and analytic mass estimates; no transport run.
Json inspectMaterials(const Project& project);
Json auditMaterialSnapshot(const Project& project);
}
