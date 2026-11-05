#include "ResponseMatrix.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Response: ")+m);}
int integer(const Json& j,const char* k,int lo,int hi){const auto& v=j.at(k);require(v.is_number_integer(),"integer required");auto n=v.get<std::int64_t>();require(n>=lo&&n<=hi,"integer outside range");return int(n);}
double number(const Json& v,double lo=0,double hi=1e300){require(v.is_number(),"number required");double x=v.get<double>();require(std::isfinite(x)&&x>=lo&&x<=hi,"number outside finite range");return x;}
Json physical(Json p){p.erase("provenance");return p;}
void validateSource(const Project& p,const ResponseOptions& o){
    p.validate();require(p.voxelScoring.is_null()&&p.sourceSampling.is_null(),"whole-target response requires an unweighted source and disabled voxel scoring");std::set<std::string> actual;
    for(const auto& s:p.spectra){require(actual.insert(s.particle).second,"duplicate incident species");require(s.interpolation=="bin_constant"&&s.energy==o.edges,"source must use exactly the cache's constant energy bins");}
    require(actual==std::set<std::string>(o.particles.begin(),o.particles.end()),"source particle set differs from response options");
}
Json moment(const Json& v,int n){int count=integer(v,"count",2,100000000),nz=integer(v,"nonzero_histories",0,count);require(count==n,"moment history count differs");
    double mean=number(v.at("mean_MeV")),m2=number(v.at("m2_MeV2"));require((mean>0)==(nz>0)&&(nz>0||m2==0),"inconsistent zero moments");
    return {{"count",n},{"nonzero_histories",nz},{"mean_MeV",mean},{"m2_MeV2",m2}};
}
}
ResponseOptions ResponseOptions::fromJson(const Json& j){
    require(j.is_object()&&j.size()==6&&j.at("schema")=="scdat.radiation-response-options/1","unsupported/missing/extra response settings");
    const std::set<std::string> keys={"schema","particles","energy_edges_MeV","histories_per_case","histories_per_batch","max_parallel"};for(auto it=j.begin();it!=j.end();++it)require(keys.count(it.key()),"unknown response setting");
    ResponseOptions o;o.historiesPerCase=integer(j,"histories_per_case",2,100000000);o.historiesPerBatch=integer(j,"histories_per_batch",2,100000000);o.maxParallel=integer(j,"max_parallel",1,8);
    require(j.at("particles").is_array()&&!j.at("particles").empty()&&j.at("particles").size()<=5,"require 1..5 particle species");std::set<std::string> species;
    for(const auto& v:j.at("particles")){auto s=v.get<std::string>();require(s=="e-"||s=="proton"||s=="alpha"||s=="gamma"||s=="neutron","unsupported particle");require(species.insert(s).second,"duplicate particle");}o.particles.assign(species.begin(),species.end());
    require(j.at("energy_edges_MeV").is_array()&&j.at("energy_edges_MeV").size()>=2&&j.at("energy_edges_MeV").size()<=33,"require 2..33 energy edges");
    for(const auto& v:j.at("energy_edges_MeV")){double x=number(v,1e-9,1e9);require(o.edges.empty()||x>o.edges.back(),"energy edges must increase strictly");o.edges.push_back(x);}
    auto cases=o.particles.size()*(o.edges.size()-1);require(cases<=32&&std::int64_t(cases)*o.historiesPerCase<=100000000,"case or total history budget exceeded");
    auto batches=(o.historiesPerCase+std::int64_t(o.historiesPerBatch)-1)/o.historiesPerBatch;if(o.historiesPerCase%o.historiesPerBatch==1)--batches;
    require(cases*batches<=10000,"total planned batch budget exceeded");return o;
}
Json ResponseOptions::toJson()const{return {{"schema","scdat.radiation-response-options/1"},{"particles",particles},{"energy_edges_MeV",edges},{"histories_per_case",historiesPerCase},{"histories_per_batch",historiesPerBatch},{"max_parallel",maxParallel}};}
BatchOptions ResponseOptions::batchOptions()const{BatchOptions b;b.historiesPerBatch=historiesPerBatch;b.maxHistories=historiesPerCase;b.maxParallel=maxParallel;b.maxSeconds=0;b.minCompletedBatches=10000;b.minNonzeroHistories=100000000;b.targetRelativeError=1e-9;return b;}
std::vector<ResponseCase> planResponseCases(const Project& base,const ResponseOptions& raw){
    auto o=ResponseOptions::fromJson(raw.toJson());validateSource(base,o);auto temp=base;temp.events=o.historiesPerCase;auto batches=planBatches(temp,o.batchOptions());std::vector<ResponseCase> cases;
    for(const auto& species:o.particles)for(std::size_t bin=0;bin+1<o.edges.size();++bin){auto p=base;int index=int(cases.size());
        p.name="Response basis "+std::to_string(index);p.environmentSource="Synthetic unit-integral response basis";p.synthetic=true;p.quantity=SpectrumQuantity::Fluence;p.duration=1;p.environmentMetadata=defaultEnvironmentMetadata();p.events=o.historiesPerCase;
        p.seed=1+(std::int64_t(base.seed)-1+std::int64_t(index)*batches.size())%2147483646;p.provenance["last_loaded_input"]=nullptr;
        p.provenance["response_basis"]={{"index",index},{"particle",species},{"low_MeV",o.edges[bin]},{"high_MeV",o.edges[bin+1]}};
        Spectrum s;s.particle=species;s.interpolation="bin_constant";s.energy={o.edges[bin],o.edges[bin+1]};s.values={1/(o.edges[bin+1]-o.edges[bin])};s.prepare();p.spectra={s};p.validate();
        cases.push_back({index,species,o.edges[bin],o.edges[bin+1],std::move(p)});
    }return cases;
}
Json responseDescriptor(const Project& p,const ResponseOptions& raw){auto o=ResponseOptions::fromJson(raw.toJson());validateSource(p,o);auto j=p.toJson();for(const char* k:{"name","provenance","environment","duration_s","events"})j.erase(k);
    return {{"schema","scdat.radiation-response-descriptor/1"},{"physical_project",j},{"source","Isotropic omnidirectional sphere, inward cosine incidence; uniform energy per bin"},{"scoring","Whole-target per-primary event moments: ionizing, total and nonionizing energy deposition in MeV; actual mass in kg"}};
}
Json responseCacheIdentity(const Project& p,const ResponseOptions& o,const std::string& sha,const Json& runtime){return {{"schema","scdat.radiation-response-identity/1"},{"descriptor",responseDescriptor(p,o)},{"options",ResponseOptions::fromJson(o.toJson()).toJson()},{"worker_sha256",sha},{"runtime_audit",runtime}};}
Json makeResponseMatrix(const Project& p,const ResponseOptions& o,const std::vector<Json>& results){auto plan=planResponseCases(p,o);require(results.size()==plan.size(),"case/result slots differ");Json rows=Json::array();std::map<std::string,double> masses;
    for(const auto& c:plan){const auto& r=results[c.index];require(r.at("status")=="completed"&&integer(r,"histories",2,100000000)==o.historiesPerCase,"incomplete basis history budget");
        require(physical(Project::fromJson(r.at("project")).toJson())==physical(c.project.toJson()),"basis result project differs");require(number(r.at("expected_primaries"))==c.project.expectedPrimaries(),"basis source normalization differs");
        require(r.at("targets").is_array()&&!r.at("targets").empty()&&r.at("score_moments").is_object(),"missing target data");Json targets=Json::array();std::set<std::string> ids;
        for(const auto& t:r.at("targets")){auto id=t.at("id").get<std::string>();require(ids.insert(id).second,"duplicate target ID");double mass=number(t.at("mass_kg"),1e-300);if(c.index==0)masses[id]=mass;require(masses.count(id)&&masses.at(id)==mass,"basis target mass or set differs");Json moments=Json::object(),coefficients=Json::object();
            for(const char* channel:{"ionizing","total","nonionizing"}){auto v=moment(r.at("score_moments").at(id).at(channel),o.historiesPerCase);moments[channel]=v;double coefficient=v.at("mean_MeV").get<double>()*1.602176634e-13/mass;require(std::isfinite(coefficient),"coefficient overflow");coefficients[channel]=coefficient;}
            targets.push_back({{"id",id},{"mass_kg",mass},{"score_moments",moments},{"coefficient_Gy_per_primary",coefficients}});
        }require(ids.size()==masses.size()&&ids.size()==r.at("score_moments").size(),"basis target set differs");rows.push_back({{"index",c.index},{"particle",c.particle},{"low_MeV",c.low},{"high_MeV",c.high},{"histories",o.historiesPerCase},{"targets",targets}});
    }return {{"schema","scdat.radiation-response-matrix/1"},{"descriptor",responseDescriptor(p,o)},{"options",o.toJson()},{"rows",rows}};
}
Json applyResponseMatrix(const Project& p,const ResponseOptions& o,const Json& matrix){auto plan=planResponseCases(p,o);require(matrix.at("schema")=="scdat.radiation-response-matrix/1"&&matrix.at("descriptor")==responseDescriptor(p,o)&&matrix.at("options")==o.toJson()&&matrix.at("rows").size()==plan.size(),"response matrix identity differs");
    struct Sum{double mass=0,dose=0,total=0,nonion=0,variance=0;bool unknown=false,sufficient=true;Json components=Json::array();};std::map<std::string,Sum> sums;
    for(const auto& c:plan){const auto& row=matrix.at("rows").at(c.index);require(row.at("index")==c.index&&row.at("particle")==c.particle&&row.at("low_MeV")==c.low&&row.at("high_MeV")==c.high&&row.at("histories")==o.historiesPerCase,"response row identity differs");
        const Spectrum* spectrum=nullptr;for(const auto& s:p.spectra)if(s.particle==c.particle)spectrum=&s;auto bin=std::size_t(std::lower_bound(o.edges.begin(),o.edges.end(),c.low)-o.edges.begin());
        double w=pi*std::pow(p.radius/10,2)*(c.high-c.low)*spectrum->values[bin]*(p.quantity==SpectrumQuantity::Flux?p.duration:1);require(std::isfinite(w)&&w>=0,"weight overflow");std::set<std::string> ids;
        for(const auto& t:row.at("targets")){auto id=t.at("id").get<std::string>();require(ids.insert(id).second,"duplicate target");double mass=number(t.at("mass_kg"),1e-300);if(c.index==0)sums[id].mass=mass;require(sums.count(id)&&sums.at(id).mass==mass,"response target set/mass differs");auto& sum=sums[id];const auto& all=t.at("score_moments");auto m=moment(all.at("ionizing"),o.historiesPerCase);double mean=m.at("mean_MeV"),m2=m.at("m2_MeV2"),factor=w*1.602176634e-13/mass;double dose=mean*factor,se=std::sqrt(m2/(double(o.historiesPerCase)*(o.historiesPerCase-1)))*factor;
            double total=moment(all.at("total"),o.historiesPerCase).at("mean_MeV").get<double>()*factor,nonion=moment(all.at("nonionizing"),o.historiesPerCase).at("mean_MeV").get<double>()*factor;
            bool zero=m.at("nonzero_histories")==0;Json rse=mean>0?Json(std::sqrt(m2/(double(o.historiesPerCase)*(o.historiesPerCase-1)))/mean):Json(nullptr);
            if(w>0){sum.unknown=sum.unknown||zero;sum.sufficient=sum.sufficient&&!zero&&m.at("nonzero_histories").get<int>()>=30&&rse.get<double>()<=.1;}
            sum.dose+=dose;sum.total+=total;sum.nonion+=nonion;sum.variance+=se*se;
            sum.components.push_back({{"index",c.index},{"particle",c.particle},{"low_MeV",c.low},{"high_MeV",c.high},{"expected_primaries",w},{"dose_Gy",dose},{"standard_error_Gy",se},{"relative_error",rse},{"nonzero_histories",m.at("nonzero_histories")},{"histories",o.historiesPerCase}});
        }require(ids.size()==sums.size(),"response target set differs");
    }Json targets=Json::array();for(const auto& pair:sums){const auto& v=pair.second;double se=std::sqrt(v.variance);require(std::isfinite(v.dose)&&std::isfinite(v.total)&&std::isfinite(v.nonion)&&std::isfinite(se)&&std::isfinite(v.dose*100),"weighted result overflow");targets.push_back({{"id",pair.first},{"mass_kg",v.mass},{"dose_Gy",v.dose},{"dose_rad",v.dose*100},{"total_dose_Gy",v.total},{"nonionizing_dose_Gy",v.nonion},{"standard_error_Gy",se},{"relative_error",!v.unknown&&v.dose>0?Json(se/v.dose):Json(nullptr)},{"quality",v.unknown?"unknown":(v.sufficient?"sufficient":"insufficient")},{"components",v.components}});}
    return {{"schema","scdat.radiation-response-result/1"},{"status","completed"},{"mode","weighted_cache"},{"targets",targets},{"quality_criteria",{{"min_nonzero_histories_per_positive_weight_case",30},{"max_relative_error_per_positive_weight_case",.1}}},{"uncertainty_rule","Independent basis variances summed in quadrature; zero-hit positive-weight basis makes RSE unknown; shared-cache projections are correlated"}};
}
}
