#include "TargetFluence.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spacerad::engine {
namespace {
void need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
bool near(double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-10*std::max(std::abs(a),std::abs(b));}
Json moment(const Moments& m){
    return {{"histories",m.count},{"nonzero_histories",m.nonzero},{"mean_weighted_length_mm",m.mean},{"m2_weighted_length_mm2",m.m2}};
}
Moments decode(const Json& j,int n){
    need(j.is_object()&&j.size()==4&&j.at("histories")==n&&j.at("nonzero_histories").is_number_integer()&&j.at("nonzero_histories")>=0&&j.at("nonzero_histories")<=n,"Invalid fluence history counts");
    need(j.at("mean_weighted_length_mm").is_number()&&j.at("m2_weighted_length_mm2").is_number(),"Invalid fluence moment type");
    Moments m;
    m.count=n;m.nonzero=j.at("nonzero_histories");m.mean=j.at("mean_weighted_length_mm");m.m2=j.at("m2_weighted_length_mm2");
    need(std::isfinite(m.mean)&&std::isfinite(m.m2)&&m.mean>=0&&m.m2>=0&&
         (m.nonzero ? m.mean>0 : m.mean==0&&m.m2==0),"Invalid fluence moments");
    return m;
}
Json row(const Moments& m,double factor){
    decode(moment(m),m.count);
    const double value=m.mean*factor,se=m.standardError()*factor;
    need(std::isfinite(value)&&std::isfinite(se)&&(!m.nonzero||value>0),"Fluence normalization overflow or underflow");
    return {{"moments",moment(m)},{"fluence_cm-2",value},
            {"standard_error_cm-2",m.nonzero?Json(se):Json(nullptr)}};
}
void checkRow(const Json& actual,const Json& expected){
    need(actual.is_object()&&actual.size()==expected.size()&&actual.at("moments")==expected.at("moments"),"Invalid fluence row");
    for(const char* key:{"fluence_cm-2","standard_error_cm-2"}){
        const auto& v=actual.at(key);const auto& e=expected.at(key);
        need(e.is_null()?v.is_null():v.is_number()&&near(v.get<double>(),e.get<double>()),"Fluence normalization or uncertainty differs from moments");
    }
}
}
const std::array<std::string,6>& fluenceSpecies(){
    static const std::array<std::string,6> species={"e-","e+","proton","alpha","gamma","neutron"};
    return species;
}
Json TargetFluenceSpec::toJson() const{return {{"schema","spacerad.target-fluence-settings/1"},{"target_id",target},{"energy_edges_MeV",edges}};}
TargetFluenceSpec TargetFluenceSpec::fromJson(const Json& j){
    need(j.is_object()&&j.size()==3&&j.at("schema")=="spacerad.target-fluence-settings/1","Unsupported target fluence settings");
    TargetFluenceSpec s;
    s.target=j.at("target_id");
    need(!s.target.empty(),"Target ID required");
    const auto& edges=j.at("energy_edges_MeV");
    need(edges.is_array()&&edges.size()>=2&&edges.size()<=129,"Require 2..129 fluence energy edges");
    for(const auto& e:edges){
        need(e.is_number(),"Fluence edges must be numeric");
        double x=e;
        need(std::isfinite(x)&&x>=0&&x<=1e9&&(s.edges.empty()||x>s.edges.back()),"Fluence energy edges must increase within 0..1e9 MeV");
        s.edges.push_back(x);
    }
    return s;
}
double TargetFluenceSpec::volume(const Project& p) const{
    need(p.voxelScoring.is_null(),"Target fluence and voxel scoring cannot be combined in this version");
    auto geometry=compileGeometry(p);
    for(const auto& v:geometry.volumes)if(v.id==target&&v.sensitive){
        double size=materialVolume(v);
        need(std::isfinite(size)&&size>0,"Invalid target volume");
        return size;
    }
    throw std::runtime_error("Fluence target must be a sensitive component ID");
}
Json makeTargetFluence(const Project& p,const TargetFluenceSpec& s,const std::string& hash,const std::vector<Moments>& m,const Moments& total){
    const auto stride=s.edges.size()+1;
    const double volume=s.volume(p),factor=p.expectedPrimaries()*100/volume;
    need(std::isfinite(factor)&&factor>0&&m.size()==fluenceSpecies().size()*stride+1&&total.count==p.events,"Invalid target fluence normalization");
    Json by=Json::object();double sum=0;
    for(size_t k=0;k<fluenceSpecies().size();++k){
        Json bins=Json::array();for(size_t i=0;i<stride;++i){
            const auto& x=m.at(k*stride+i);need(x.count==p.events,"Fluence bins must include zero histories");sum+=x.mean;
            bins.push_back(row(x,factor));
        }
        by[fluenceSpecies()[k]]=std::move(bins);
    }
    need(m.back().count==p.events&&near(sum+m.back().mean,total.mean),"Fluence bins do not conserve total track length");
    return {{"schema","spacerad.target-fluence/1"},{"settings",s.toJson()},{"settings_sha256",hash},
            {"volume_mm3",volume},{"estimator","volume_track_length_pre_step_kinetic_energy"},
            {"bins","underflow, [lower,upper), overflow; other species aggregated separately"},
            {"total",row(total,factor)},{"species",by},{"other_species",row(m.back(),factor)}};
}
void validateTargetFluence(const Project& p,const Json& result,const Json& settings,const std::string& hash){
    if(settings.is_null()){need(!result.contains("target_fluence"),"Unexpected target fluence without settings snapshot");return;}
    auto s=TargetFluenceSpec::fromJson(settings);
    const auto& field=result.at("target_fluence");
    const auto& by=field.at("species");need(by.is_object()&&by.size()==fluenceSpecies().size(),"Unexpected target fluence species");
    std::vector<Moments> moments;
    for(const auto& name:fluenceSpecies()){
        const auto& bins=by.at(name);need(bins.is_array()&&bins.size()==s.edges.size()+1,"Target fluence bin count differs");
        for(const auto& b:bins)moments.push_back(decode(b.at("moments"),p.events));
    }
    moments.push_back(decode(field.at("other_species").at("moments"),p.events));
    const auto expected=makeTargetFluence(p,s,hash,moments,decode(field.at("total").at("moments"),p.events));
    need(field.size()==expected.size()&&field.at("volume_mm3").is_number()&&near(field.at("volume_mm3"),expected.at("volume_mm3")),"Fluence volume differs from project");
    for(const char* key:{"schema","settings","settings_sha256","estimator","bins"})need(field.at(key)==expected.at(key),"Fluence metadata differs from settings");
    for(const char* key:{"total","other_species"})checkRow(field.at(key),expected.at(key));
    for(const auto& name:fluenceSpecies())for(size_t i=0;i<by.at(name).size();++i)checkRow(by.at(name)[i],expected.at("species").at(name)[i]);
}
}
