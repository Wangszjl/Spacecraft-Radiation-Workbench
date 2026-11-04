#include "spacerad/VoxelScoring.h"
#include "Voxels.h"
#include <stdexcept>

namespace spacerad {
Project configureVoxelScoring(const Project& base,const std::string& volumeId,
                              const std::array<int,3>& counts,
                              const std::vector<double>& edges,
                              int minimumNonzeroHistories,double maximumRelativeError) {
    Project result=base;
    result.voxelScoring={{"schema","scdat.voxel-scoring/1"},{"volume_id",volumeId},
        {"counts",counts},{"edep_edges_MeV",edges},
        {"quality",{{"min_nonzero_histories",minimumNonzeroHistories},{"max_relative_error",maximumRelativeError}}}};
    (void)engine::voxelSpec(result.voxelScoring);
    result.validate();
    return result;
}
Project disableVoxelScoring(const Project& base) {
    Project result=base;result.voxelScoring=nullptr;result.validate();return result;
}
}
