#pragma once
#include "RadiationProject.h"
#include <map>
#include <utility>

namespace spacerad::engine {
struct ImportanceSample {
    double physicalQuantile;
    double weight;
    bool tail;
};
struct ImportanceSampling {
    double physicalFraction;
    double tailFraction;
    std::map<std::string,double> proposalSpecies;
    static ImportanceSampling fromJson(const Json&);
    Json proposalSpeciesFractions(const Project&) const;
    std::pair<size_t,double> sampleSpecies(double,const Project&) const;
    double speciesWeight(const std::string&,const Project&) const;
    double proposalTailFraction() const;
    double bodyWeight() const;
    double tailWeight() const;
    ImportanceSample sample(double uniform) const;
};
void validateImportanceResult(const Project&, const Json&);
}
