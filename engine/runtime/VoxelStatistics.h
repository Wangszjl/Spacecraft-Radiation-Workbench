#pragma once
#include "RadiationProject.h"
namespace spacerad::engine {
Json makeVoxelField(const Project&,const Json& geometryAudit,const Json& cellMoments,int histories);
void validateVoxelResult(const Project&,const Json& result);
void mergeVoxelResults(const Project&,const std::vector<Json>& results,Json& merged);
Json makeEdepSpectrum(const std::vector<double>& edges,const std::vector<std::int64_t>& counts,std::int64_t overflow,int histories);
}
