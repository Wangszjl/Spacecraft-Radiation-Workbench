#pragma once
#include "RadiationProject.h"
namespace spacerad::engine {
struct VoxelSpec {
    std::string volumeId;
    std::array<int,3> counts;
    std::vector<double> edges;
    double maxRse;
    int minNonzero;
    int size() const{return counts[0]*counts[1]*counts[2];}
};
VoxelSpec voxelSpec(const Json&);
void validateVoxels(const Project&);
Json voxelLayout(const Project&);
// Append zero histories in one Chan update; targetCount is the final count.
void padZeroHistories(Moments&,int targetCount);
}
