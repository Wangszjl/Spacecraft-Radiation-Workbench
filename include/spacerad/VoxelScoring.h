#pragma once
#include "Project.h"
#include <array>

namespace spacerad {
// Enable exact, uniform-mass scoring inside a homogeneous solid sensitive box.
// The input remains unchanged and the complete project is validated.
Project configureVoxelScoring(const Project& base,const std::string& volumeId,
                              const std::array<int,3>& counts,
                              const std::vector<double>& depositedEnergyEdgesMeV,
                              int minimumNonzeroHistories=100,
                              double maximumRelativeError=0.2);
Project disableVoxelScoring(const Project& base);
}
