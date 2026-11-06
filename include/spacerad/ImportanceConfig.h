#pragma once
#include "Project.h"
#include <map>
namespace spacerad {
Project configureImportanceSampling(const Project&,double physicalFraction,double tailFraction,
                                   const std::map<std::string,double>& proposalSpecies={});
Project disableImportanceSampling(const Project&);
}
