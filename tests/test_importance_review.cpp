#include "spacerad/ImportanceReview.h"
#include "ImportanceFixture.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace spacerad;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool near(double a,double b){return std::abs(a-b)<=1e-10*std::max(std::abs(a),std::abs(b));}
template<class F> void rejects(F run){try{run();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection");}
}
int main()try{
    auto p=Project::demo();p.events=4;
    p.volumes={{"Chip","G4_Si",{0,0,0},{1,1,1},0,true}};
    p.sourceSampling={{"schema","spacerad.source-importance/1"},{"physical_fraction",.5},{"tail_fraction",.2}};
    Spectrum spectrum;spectrum.particle="proton";spectrum.energy={1,3};spectrum.values={1,1};spectrum.prepare();p.spectra={spectrum};
    auto result=testing::weightedFixture(p);
    result["schema"]="scdat.radiation-result/2";result["status"]="completed";result["project"]=p.toJson();
    const auto before=p.toJson(),raw=result;
    auto review=importanceReview(p,result,2,1);
    check(p.toJson()==before&&result==raw,"Review modified physical input or result");
    const auto& target=review.at("targets").at(0);
    check(target.at("status")=="insufficient"&&target.at("precision_met")==true&&target.at("low_hit_regions").size()==2,
          "Low support was hidden by acceptable observed precision");
    const auto& groups=target.at("groups");
    const double factor=p.expectedPrimaries()*1.602176634e-13;
    check(groups.size()==2&&near(groups[0].at("tid_contribution_Gy"),.25*factor)&&near(groups[1].at("tid_contribution_Gy"),.125*factor),
          "Regional dose failed to use the full event denominator");
    check(near(target.at("sum_contributions_Gy"),.375*factor),"Contributions do not reconstruct target dose");
    const double totalVariance=std::pow(target.at("observed_standard_error_Gy").get<double>(),2);
    const double individual=target.at("sum_individual_contribution_variances_Gy2");
    const double covariance=target.at("cross_covariance_contribution_Gy2");
    check(individual>totalVariance&&covariance<0&&near(individual+covariance,totalVariance),
          "Shared-history covariance was ignored");
    check(importanceReview(p,result,1,1).at("all_targets_pass")==true,"Sufficient fixture failed configured screen");
    check(importanceReview(p,result,1,.2).at("all_targets_pass")==false,"Precision threshold ignored");
    rejects([&]{importanceReview(p,result,0,.2);});
    auto bad=result;bad["source_importance"]["target_support"]["Chip"]["proton"]["body"]["score_moments"][0]["mean_MeV"]=99;
    rejects([&]{importanceReview(p,bad);});
    auto changed=result;changed["project"]["duration_s"]=p.duration*2;
    rejects([&]{importanceReview(p,changed);});
    check(importanceReview(Project::demo(),Json::object()).at("status")=="not_applicable","Nominal project was mislabeled weighted");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
