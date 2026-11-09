#pragma once
#include "RadiationProject.h"
#include <map>
#include <optional>
#include <tuple>
namespace spacerad::engine {
struct TargetFluenceSpec {
    std::string target;
    std::vector<double> edges;
    bool identifyOtherSpecies=false;
    Json responseTable=nullptr;
    std::vector<std::optional<double>> responseCoefficients;
    Json toJson() const;
    static TargetFluenceSpec fromJson(const Json&);
    double volume(const Project&) const;
};
const std::array<std::string,6>& fluenceSpecies();
using OtherFluenceBin=std::tuple<std::string,int,size_t>;
using OtherFluenceMoments=std::map<OtherFluenceBin,Moments>;
constexpr size_t maxOtherFluenceBins=4096;
Json makeTargetFluence(const Project&,const TargetFluenceSpec&,const std::string& settingsHash,
                      const std::vector<Moments>&,const Moments& total,const Moments* response=nullptr,
                      const OtherFluenceMoments& detail={});
void validateTargetFluence(const Project&,const Json& result,const Json& settings,
                           const std::string& settingsHash);
}
