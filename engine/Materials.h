#pragma once
#include "RadiationProject.h"
namespace spacerad::engine {
// Pure model validation. NIST lookup and SDK equality are checked in the Worker.
void validateMaterialSnapshot(const Project& project);
std::vector<std::string> unassignedMaterialIds(const Project& project);
}
