#include "spacerad/ImportanceReview.h"
#include <spacerad/engine/SourceSampling.h>
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>

namespace spacerad {
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
Json importanceReview(const Project& p,const Json& result,int minimum,double maximum) {
    need(minimum>=1&&minimum<=100000000&&std::isfinite(maximum)&&maximum>0&&maximum<=1,
         "Invalid importance-review screening thresholds");
    p.validate();
    engine::validateImportanceResult(p,result);
    if(p.sourceSampling.is_null()) return {
        {"schema","spacerad.importance-review/1"},{"status","not_applicable"},
        {"targets",Json::array()},{"all_targets_pass",false}};
    need(result.at("schema")=="scdat.radiation-result/2"&&result.at("status")=="completed",
         "Importance review requires a completed result");
    auto actual=Project::fromJson(result.at("project")).toJson(),expected=p.toJson();
    actual.erase("provenance");expected.erase("provenance");
    need(actual==expected,"Importance review project differs from the completed result");
    std::set<std::string> ids;
    const auto geometry=engine::compileGeometry(p);
    for(const auto& volume:geometry.volumes)if(volume.sensitive)ids.insert(volume.id);
    for(const auto& target:result.at("targets"))
        need(ids.erase(target.at("id").get<std::string>())==1,"Unexpected or duplicate importance review target");
    need(ids.empty(),"Missing importance review target");
    const int n=p.events;
    Json targets=Json::array();
    int sufficient=0;
    for(const auto& target:result.at("targets")){
        const auto& id=target.at("id").get_ref<const std::string&>();
        const double factor=p.expectedPrimaries()*1.602176634e-13/target.at("mass_kg").get<double>();
        const double total=target.at("tid_Gy"),totalSe=target.at("tid_standard_error_Gy");
        const auto& support=result.at("source_importance").at("target_support").at(id);
        Json groups=Json::array(),low=Json::array(),zeros=Json::array();
        double sum=0,sumIndividualVariance=0;
        for(const auto& [particle,regions]:support.items()){
            const auto spectrum=std::find_if(p.spectra.begin(),p.spectra.end(),[&](const Spectrum& s){return s.particle==particle;});
            need(spectrum!=p.spectra.end(),"Unknown importance-review species");
            const double tail=engine::ImportanceSampling::fromJson(p.sourceSampling).tailFraction;
            const double threshold=spectrum->sample(1-tail);
            for(const char* region:{"body","tail"}){
                const auto& group=regions.at(region);
                const int count=group.at("histories"),hits=group.at("nonzero_histories");
                const auto& raw=group.at("score_moments").at(0);
                const double conditionalMean=raw.at("mean_MeV"),conditionalM2=raw.at("m2_MeV2");
                const double mean=double(count)/n*conditionalMean;
                const double m2=conditionalM2+double(count)*conditionalMean*conditionalMean-double(n)*mean*mean;
                const double dose=mean*factor;
                const double se=std::sqrt(std::max(0.0,m2)/(double(n)*(n-1)))*factor;
                const bool body=std::string(region)=="body";
                const bool enough=hits>=minimum;
                const std::string key=particle+"/"+region;
                if(!enough) low.push_back(key);
                if(hits==0) zeros.push_back(key);
                groups.push_back({{"particle",particle},{"region",region},
                    {"energy_range_MeV",Json::array({body?spectrum->energy.front():threshold,body?threshold:spectrum->energy.back()})},
                    {"sampled_histories",count},{"nonzero_histories",hits},
                    {"tid_contribution_Gy",dose},{"observed_contribution_standard_error_Gy",se},
                    {"fraction_of_observed_target_dose",total>0?Json(dose/total):Json(nullptr)},
                    {"status",hits==0?"zero_hit":enough?"sufficient_hits":"low_hits"}});
                sum+=dose;sumIndividualVariance+=se*se;
            }
        }
        need(std::abs(sum-total)<=1e-8*std::max({std::abs(sum),std::abs(total),1e-280}),
             "Importance review contributions do not reconstruct target TID");
        const Json rse=target.at("relative_standard_error");
        const bool precision=!rse.is_null()&&rse.get<double>()<=maximum;
        const bool pass=low.empty()&&precision;
        sufficient+=pass;
        targets.push_back({{"id",id},{"mass_kg",target.at("mass_kg")},{"tid_Gy",total},{"observed_standard_error_Gy",totalSe},
            {"observed_relative_standard_error",rse},{"status",total==0?"zero_hit":pass?"passes_screen":"insufficient"},
            {"passes_screen",pass},{"precision_met",precision},{"low_hit_regions",low},{"zero_hit_regions",zeros},
            {"groups",groups},{"sum_contributions_Gy",sum},
            {"sum_individual_contribution_variances_Gy2",sumIndividualVariance},
            {"cross_covariance_contribution_Gy2",totalSe*totalSe-sumIndividualVariance}});
    }
    return {{"schema","spacerad.importance-review/1"},{"status","reviewed"},{"histories",n},
        {"source_sampling",p.sourceSampling},{"expected_primaries",p.expectedPrimaries()},
        {"project_sha256",result.value("project_sha256",Json(nullptr))},
        {"engine_version",result.value("engine_version",Json(nullptr))},
        {"policy",{{"min_nonzero_per_region",minimum},{"max_total_relative_standard_error",maximum}}},
        {"targets",targets},{"passing_targets",sufficient},{"all_targets_pass",sufficient==int(targets.size())&&!targets.empty()},
        {"scope","Observed MC screen; low- and zero-hit source regions do not bound unseen dose; no independent physics validation"},
        {"covariance_rule","Regions share histories and are mutually exclusive; individual region variances must not be added as independent estimates"}};
}
}
