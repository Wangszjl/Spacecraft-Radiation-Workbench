#include "VoxelStatistics.h"
#include "Voxels.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Voxel statistics: ")+m);}
bool near(double x,double y){return std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-10*std::max({std::abs(x),std::abs(y),1e-288});}
int count(const Json& j,int low,int high){require(j.is_number_integer(),"integer count required");auto n=j.get<std::int64_t>();require(n>=low&&n<=high,"count outside range");return int(n);}
double number(const Json& j){require(j.is_number(),"numeric moment required");auto n=j.get<double>();require(std::isfinite(n)&&n>=0,"negative/nonfinite moment");return n;}
struct Pool {
    int n=0,nz=0;double mean=0,m2=0;
    void add(const Json& j,int histories){int k=count(j.at("count"),2,100000000),z=count(j.at("nonzero_histories"),0,k);require(k==histories&&n<=100000000-k,"moment histories differ");
        double a=number(j.at("mean_MeV")),b=number(j.at("m2_MeV2"));require((a==0)==(z==0)&&((z!=0)||(b==0)),"zero-hit moment inconsistency");
        double delta=a-mean;int total=n+k;m2+=b+delta*delta*n*double(k)/total;mean+=delta*k/total;n=total;nz+=z;require(std::isfinite(mean)&&std::isfinite(m2),"moment overflow");}
    Json json()const{return {{"count",n},{"nonzero_histories",nz},{"mean_MeV",mean},{"m2_MeV2",m2}};}
    double se()const{return std::sqrt(m2/(double(n)*(n-1)));}
};
const char* types[]={"ionizing","total","nonionizing"};
bool jsonNear(const Json& a,const Json& b){if(a.is_number()&&b.is_number())return near(a.get<double>(),b.get<double>());if(a.type()!=b.type())return a==b;
    if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!jsonNear(a[i],b[i]))return false;return true;}
    if(a.is_object()){if(a.size()!=b.size())return false;for(auto it=a.begin();it!=a.end();++it)if(!b.contains(it.key())||!jsonNear(it.value(),b.at(it.key())))return false;return true;}return a==b;}
}
Json makeEdepSpectrum(const std::vector<double>& edges,const std::vector<std::int64_t>& bins,std::int64_t overflow,int histories){
    require(histories>=2&&edges.size()==bins.size()+1&&overflow>=0,"invalid spectrum dimensions/count");std::int64_t total=overflow;Json probability=Json::array();
    for(auto n:bins){require(n>=0&&n<=histories,"invalid spectrum bin count");total+=n;probability.push_back(double(n)/histories);}require(total==histories,"spectrum omits or duplicates events");
    return {{"schema","scdat.event-edep-spectrum/1"},{"quantity","ionizing_edep_per_source_event"},{"edges_MeV",edges},{"counts",bins},{"overflow",overflow},
        {"histories",histories},{"probabilities",probability},{"overflow_probability",double(overflow)/histories},
        {"semantics","[low,high) energy bins; last upper edge goes to overflow; all zero events in first bin; event probability, not particle fluence or a per-MeV density"}};
}
Json makeVoxelField(const Project& p,const Json& audit,const Json& moments,int histories){
    auto spec=voxelSpec(p.voxelScoring);auto layout=voxelLayout(p);require(histories>=2&&histories<=p.events&&moments.is_array()&&moments.size()==spec.size(),"cell/history count differs");
    for(auto it=layout.begin();it!=layout.end();++it)require(audit.contains(it.key())&&jsonNear(it.value(),audit.at(it.key())),"actual voxel layout differs from project");
    double mass=number(audit.at("cell_mass_kg")),wholeMass=number(audit.at("target_mass_kg")),density=number(audit.at("density_kg_m3"));
    require(mass>0&&near(mass*spec.size(),wholeMass)&&near(mass,density*layout.at("cell_volume_mm3").get<double>()*1e-9),"cell mass/density differs from actual target");
    Json field=audit;field["schema"]="scdat.radiation-voxel-field/1";field["geometry_audit"]=audit;field["histories"]=histories;field["expected_primaries"]=p.expectedPrimaries();
    field["quality_criteria"]=p.voxelScoring.at("quality");field["rows"]=Json::array();int hotspot=0;double highest=-1;
    for(int i=0;i<spec.size();++i){Pool scores[3];for(int k=0;k<3;++k)scores[k].add(moments.at(i).at(types[k]),histories);
        double factor=1.602176634e-13*p.expectedPrimaries()/mass,dose=scores[0].mean*factor,error=scores[0].se()*factor;
        require(std::isfinite(dose)&&std::isfinite(error),"dose overflow");Json rse=scores[0].mean>0?Json(scores[0].se()/scores[0].mean):Json(nullptr);
        std::string quality=scores[0].nz==0?"zero_hit":scores[0].nz>=spec.minNonzero&&rse.get<double>()<=spec.maxRse?"sufficient":"insufficient";
        std::array<int,3> ijk{i%spec.counts[0],(i/spec.counts[0])%spec.counts[1],i/(spec.counts[0]*spec.counts[1])};
        field["rows"].push_back({{"index",i},{"ijk",ijk},{"mass_kg",mass},{"tid_Gy",dose},{"tid_rad",dose*100},{"tid_standard_error_Gy",error},
            {"relative_standard_error",rse},{"nonzero_histories",scores[0].nz},{"quality",quality},{"score_moments",moments.at(i)}});
        if(dose>highest){highest=dose;hotspot=i;}
    }
    const auto& top=field.at("rows").at(hotspot);field["hotspot"]={{"index",hotspot},{"ijk",top.at("ijk")},{"tid_Gy",top.at("tid_Gy")},{"quality",top.at("quality")},
        {"semantics","Maximum sampled cell estimate; not a validated or statistically significant hotspot"}};
    field["caution"]="Exact homogeneous box cells; zero hits are not zero risk; cell covariance is not exported; sum of cell standard errors is not target error; no interpolation or external physics validation";
    return field;
}
void validateVoxelResult(const Project& p,const Json& result){
    if(p.voxelScoring.is_null()){require(!result.contains("voxel_field")&&!result.contains("edep_spectra"),"unexpected voxel data for disabled scorer");return;}
    int histories=count(result.at("histories"),2,p.events);const auto& field=result.at("voxel_field");require(field.at("schema")=="scdat.radiation-voxel-field/1","unsupported field schema");
    Json moments=Json::array();for(const auto& row:field.at("rows"))moments.push_back(row.at("score_moments"));
    require(jsonNear(makeVoxelField(p,field.at("geometry_audit"),moments,histories),field),"field differs from raw event moments");
    const auto& spec=voxelSpec(p.voxelScoring);const Json* target=nullptr;for(const auto& row:result.at("targets"))if(row.at("id")==spec.volumeId)target=&row;
    require(target&&near(target->at("mass_kg"),field.at("target_mass_kg")),"field target mass differs");
    for(const char* type:types){double sum=0;for(const auto& m:moments)sum+=m.at(type).at("mean_MeV").get<double>();
        require(near(sum,result.at("score_moments").at(spec.volumeId).at(type).at("mean_MeV")),"cell/whole-target energy conservation failed");}
    require(result.at("edep_spectra").is_object()&&result.at("edep_spectra").size()==result.at("targets").size(),"target spectrum set differs");
    for(const auto& t:result.at("targets")){const auto& spectrum=result.at("edep_spectra").at(t.at("id").get<std::string>());
        auto bins=spectrum.at("counts").get<std::vector<std::int64_t>>();require(spectrum.at("edges_MeV")==spec.edges,"histogram edges differ");
        auto overflow=count(spectrum.at("overflow"),0,histories);require(makeEdepSpectrum(spec.edges,bins,overflow,histories)==spectrum,"spectrum probabilities/counts differ");}
}
void mergeVoxelResults(const Project& p,const std::vector<Json>& results,Json& merged){
    if(p.voxelScoring.is_null()){for(const auto& r:results)validateVoxelResult(p,r);return;}
    auto spec=voxelSpec(p.voxelScoring);std::vector<std::array<Pool,3>> cells(spec.size());int histories=0;Json histograms=Json::object();
    const auto& audit=results.front().at("voxel_field").at("geometry_audit");
    for(const auto& r:results){auto child=p;child.events=r.at("histories");validateVoxelResult(child,r);require(r.at("voxel_field").at("geometry_audit")==audit,"batch cell geometry/mass differs");int n=r.at("histories");histories+=n;
        for(int i=0;i<spec.size();++i)for(int k=0;k<3;++k)cells[i][k].add(r.at("voxel_field").at("rows").at(i).at("score_moments").at(types[k]),n);
        for(auto it=r.at("edep_spectra").begin();it!=r.at("edep_spectra").end();++it){if(!histograms.contains(it.key()))histograms[it.key()]={{"counts",std::vector<std::int64_t>(spec.edges.size()-1,0)},{"overflow",0}};
            auto& h=histograms[it.key()];for(std::size_t i=0;i<spec.edges.size()-1;++i)h["counts"][i]=h["counts"][i].get<std::int64_t>()+it.value().at("counts").at(i).get<std::int64_t>();h["overflow"]=h["overflow"].get<std::int64_t>()+it.value().at("overflow").get<std::int64_t>();}
    }
    Json moments=Json::array();for(const auto& cell:cells)moments.push_back({{"ionizing",cell[0].json()},{"total",cell[1].json()},{"nonionizing",cell[2].json()}});
    merged["voxel_field"]=makeVoxelField(p,audit,moments,histories);merged["edep_spectra"]=Json::object();
    for(auto it=histograms.begin();it!=histograms.end();++it)merged["edep_spectra"][it.key()]=makeEdepSpectrum(spec.edges,it.value().at("counts").get<std::vector<std::int64_t>>(),it.value().at("overflow").get<std::int64_t>(),histories);
    validateVoxelResult(p,merged);
}
}
