#pragma once
#include <spacerad/engine/SourceSampling.h>
#include <cmath>

namespace spacerad::testing {
using namespace spacerad::engine;
inline Json moment(int nz,double mean,double m2,int n=4) {
    return {{"count",n},{"nonzero_histories",nz},{"mean_MeV",mean},{"m2_MeV2",m2}};
}
inline Json weightedFixture(const Project& p) {
    auto cfg=ImportanceSampling::fromJson(p.sourceSampling);
    const int body=2,tail=2,n=body+tail;
    const double weightSum=body*cfg.bodyWeight()+tail*cfg.tailWeight();
    const double squareSum=body*std::pow(cfg.bodyWeight(),2)+tail*std::pow(cfg.tailWeight(),2);
    const double mean=.375,m2=.6875,se=std::sqrt(m2/(n*(n-1)));
    const double factor=p.expectedPrimaries()*1.602176634e-13;
    Json bodyScores=Json::array({moment(1,.5,.5,2),moment(1,.5,.5,2),moment(0,0,0,2)});
    Json tailScores=Json::array({moment(1,.25,.125,2),moment(1,.25,.125,2),moment(0,0,0,2)});
    Json result;
    result["histories"]=n;
    result["expected_primaries"]=p.expectedPrimaries();
    result["source_diagnostics"]={{"histories",n},{"species_histories",{{"proton",n}}}};
    result["source_importance"]={{"schema","spacerad.source-importance-result/1"},
        {"configuration",p.sourceSampling},{"proposal_tail_fraction",cfg.proposalTailFraction()},
        {"proposal_species_fractions",cfg.proposalSpeciesFractions(p)},
        {"body_histories",body},{"tail_histories",tail},{"sum_weights",weightSum},
        {"sum_squared_weights",squareSum},{"weight_ess",weightSum*weightSum/squareSum},
        {"mean_physical_primary_energy_MeV",2},
        {"mean_physical_primary_energy_standard_error_MeV",.1},
        {"tail_histories_by_species",{{"proton",tail}}},
        {"target_support",Json::object()}};
    result["source_importance"]["target_support"]["Chip"]["proton"]["body"]={{"histories",body},{"nonzero_histories",1},{"score_moments",bodyScores}};
    result["source_importance"]["target_support"]["Chip"]["proton"]["tail"]={{"histories",tail},{"nonzero_histories",1},{"score_moments",tailScores}};
    result["targets"]=Json::array({Json{
        {"id","Chip"},{"mass_kg",1},{"nonzero_histories",2},{"tid_Gy",mean*factor},
        {"tid_rad",mean*factor*100},{"tid_standard_error_Gy",se*factor},
        {"total_absorbed_Gy",mean*factor},{"nonionizing_Gy",0},{"relative_standard_error",se/mean}}});
    result["score_moments"]["Chip"]={{"ionizing",moment(2,mean,m2)},
        {"total",moment(2,mean,m2)},{"nonionizing",moment(0,0,0)}};
    return result;
}
}
