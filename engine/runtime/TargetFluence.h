#pragma once
#include "RadiationProject.h"
#include <optional>
namespace spacerad::engine {
struct TargetFluenceSpec {
    std::string target;
    std::vector<double> edges;
    Json responseTable=nullptr;
    std::vector<std::optional<double>> responseCoefficients;
    Json toJson() const;
    static TargetFluenceSpec fromJson(const Json&);
    double volume(const Project&) const;
};
const std::array<std::string,6>& fluenceSpecies();
Json makeTargetFluence(const Project&,const TargetFluenceSpec&,const std::string& settingsHash,
                      const std::vector<Moments>&,const Moments& total,const Moments* response=nullptr);
void validateTargetFluence(const Project&,const Json& result,const Json& settings,
                           const std::string& settingsHash);
}
