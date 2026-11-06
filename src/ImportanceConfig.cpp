#include "spacerad/ImportanceConfig.h"
#include <stdexcept>
namespace spacerad {
Project configureImportanceSampling(const Project& source,double physical,double tail,
                                   const std::map<std::string,double>& species){
    source.validate();
    if(!source.voxelScoring.is_null())throw std::runtime_error("Disable voxel scoring before configuring source importance sampling");
    auto p=source;
    p.sourceSampling={{"schema","spacerad.source-importance/1"},{"physical_fraction",physical},{"tail_fraction",tail}};
    if(!species.empty())p.sourceSampling["proposal_species_fractions"]=species;
    p.validate();return p;
}
Project disableImportanceSampling(const Project& source){
    source.validate();auto p=source;p.sourceSampling=nullptr;p.validate();return p;
}
}
