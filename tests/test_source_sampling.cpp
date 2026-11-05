#include <spacerad/engine/SourceSampling.h>
#include "ImportanceFixture.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>

using namespace spacerad::engine;
using spacerad::testing::weightedFixture;

namespace {
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
template<class F> void reject(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}


}

int main() try {
    auto p=Project::demo();
    const auto nominal=p.toJson();
    const auto exposure=p.expectedPrimaries();
    p.sourceSampling={{"schema","spacerad.source-importance/1"},{"physical_fraction",0.2},{"tail_fraction",0.001}};
    const auto weighted=p.toJson();
    check(weighted.at("schema")=="scdat.radiation/8","Missing opt-in schema");
    check(Project::fromJson(weighted).toJson()==weighted,"Weighted project roundtrip");
    check(p.expectedPrimaries()==exposure && weighted.at("environment")==nominal.at("environment"),"Proposal changed physical exposure");
    auto invalid=nominal;
    invalid["source_sampling"]=p.sourceSampling;
    reject([&]{Project::fromJson(invalid);});
    invalid=weighted;
    invalid["source_sampling"]["physical_fraction"]=0;
    reject([&]{Project::fromJson(invalid);});
    invalid=weighted;
    invalid["voxel_scoring"]=Json::object();
    reject([&]{Project::fromJson(invalid);});
    reject([&]{validateImportanceResult(p,Json::object());});
    reject([&]{validateImportanceResult(Project::demo(),{{"source_importance",Json::object()}});});
    p.events=4;p.sourceSampling={{"schema","spacerad.source-importance/1"},{"physical_fraction",0.5},{"tail_fraction",0.2}};
    Spectrum uniform;uniform.particle="proton";uniform.energy={1,3};uniform.values={1,1};uniform.prepare();p.spectra={uniform};
    auto fixture=weightedFixture(p);
    validateImportanceResult(p,fixture);
    auto badMoments=fixture;
    badMoments["source_importance"]["target_support"]["Chip"]["proton"]["tail"]["score_moments"][0]["m2_MeV2"]=0;
    reject([&]{validateImportanceResult(p,badMoments);});
    auto badRegion=fixture;
    badRegion["source_importance"]["target_support"]["Chip"]["proton"]["body"]["histories"]=3;
    reject([&]{validateImportanceResult(p,badRegion);});
    auto badSpecies=fixture;
    badSpecies["source_diagnostics"]["species_histories"]["proton"]=3;
    reject([&]{validateImportanceResult(p,badSpecies);});

    p.events=100000;
    auto cfg=ImportanceSampling::fromJson(p.sourceSampling);
    double sums[3]{};
    int tails=0;
    constexpr int n=200000;
    for(int i=0;i<n;++i){
        auto s=cfg.sample((i+0.5)/n);
        check(s.physicalQuantile>=0 && s.physicalQuantile<1 && s.weight>0,"Proposal loses support");
        check(s.tail==(s.physicalQuantile>=1-cfg.tailFraction),"Proposal region mismatch");
        tails+=s.tail;
        sums[0]+=s.weight;
        sums[1]+=s.weight*s.physicalQuantile;
        sums[2]+=s.weight*s.physicalQuantile*s.physicalQuantile;
    }
    check(std::abs(double(tails)/n-cfg.proposalTailFraction())<1e-5,"Incorrect high-energy allocation");
    check(std::abs(sums[0]/n-1)<1e-10 && std::abs(sums[1]/n-0.5)<1e-9 &&
          std::abs(sums[2]/n-1.0/3)<1e-9,"Likelihood weights do not recover physical distribution");
    Project mixed=Project::demo();
    Spectrum electron;electron.particle="e-";electron.energy={1,3};electron.values={1,1};electron.prepare();
    mixed.spectra.push_back(electron);
    mixed.sourceSampling={{"schema","spacerad.source-importance/1"},{"physical_fraction",0.5},{"tail_fraction",0.2},
        {"proposal_species_fractions",{{"proton",.9},{"e-",.1}}}};
    mixed.validate();
    auto mixedJson=mixed.toJson();
    check(Project::fromJson(mixedJson).toJson()==mixedJson,"Species-proposal project roundtrip");
    auto incomplete=mixedJson;
    incomplete["source_sampling"]["proposal_species_fractions"].erase("e-");
    reject([&]{Project::fromJson(incomplete);});
    auto multi=ImportanceSampling::fromJson(mixed.sourceSampling);
    const double protonPhysical=mixed.spectra[0].integral/mixed.integratedSpectrum();
    double proton=0,electronWeight=0;
    for(int i=0;i<n;++i){auto selected=multi.sampleSpecies((i+.5)/n,mixed);
        const auto& particle=mixed.spectra[selected.first].particle;
        if(particle=="proton")proton+=selected.second;else electronWeight+=selected.second;}
    check(std::abs(proton/n-protonPhysical)<1e-5 && std::abs(electronWeight/n-(1-protonPhysical))<1e-5,
          "Species likelihood weights do not recover the physical mixture");
    auto fractions=multi.proposalSpeciesFractions(mixed);
    check(fractions.at("proton")==.9 && fractions.at("e-")==.1,"Reported species proposal differs");
    return 0;
} catch (const std::exception& e) {
    std::cerr<<e.what()<<'\n';
    return 1;
}
