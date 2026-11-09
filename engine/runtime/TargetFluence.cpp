#include "TargetFluence.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
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
void checkResponse(const Json& actual,const Json& expected){
    need(actual.is_object()&&actual.size()==expected.size(),"Unexpected history response fields");
    for(const auto& [key,value]:expected.items()){
        const auto& got=actual.at(key);
        need(value.is_number_float()?got.is_number()&&near(got,value):got==value,
             "History response differs from reconstructed value or uncertainty");
    }
}
std::vector<std::optional<double>> coefficients(const TargetFluenceSpec& s){
    const auto& t=s.responseTable;
    need(t.is_object()&&t.size()==6&&t.at("schema")=="spacerad.device-response-table/1","Invalid history response table schema");
    const auto& metric=t.at("metric");
    need((metric=="ddd"||metric=="see")&&t.at("response_unit")==
         (metric=="ddd"?"MeV_cm2_per_g":"cm2_per_device")&&
         t.at("angular_basis")=="isotropic_omnidirectional","Invalid history response metric, unit or angular basis");
    const auto& source=t.at("source_file");
    need(source.is_object()&&source.size()==3&&source.at("file_name").is_string()&&
         !source.at("file_name").get_ref<const std::string&>().empty()&&source.at("size_bytes").is_number_integer()&&
         source.at("size_bytes")>0&&source.at("size_bytes")<=8*1024*1024&&source.at("sha256").is_string(),"Invalid response file identity");
    const auto& hash=source.at("sha256").get_ref<const std::string&>();
    need(hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](char c){return (c>='a'&&c<='f')||(c>='0'&&c<='9');}),"Invalid response file SHA-256");
    const auto& curves=t.at("curves");const auto& species=fluenceSpecies();
    need(curves.is_array()&&!curves.empty()&&curves.size()<=species.size(),"Invalid history response curve count");
    const size_t stride=s.edges.size()+1;
    std::vector<std::optional<double>> values(species.size()*stride+1);
    std::set<std::string> used;
    for(const auto& c:curves){
        need(c.is_object()&&c.size()==5&&c.at("device_id")==s.target&&
             c.at("reference").is_string()&&!c.at("reference").get_ref<const std::string&>().empty(),
             "History response curves must refer to the scored target and include a reference");
        auto name=c.at("particle").get<std::string>();
        auto it=std::find(species.begin(),species.end(),name);
        need(it!=species.end()&&used.insert(name).second,"Unsupported or duplicate history response species");
        const auto& es=c.at("energy_MeV");const auto& rs=c.at("response");
        need(es.is_array()&&rs.is_array()&&es.size()==rs.size()&&es.size()>=2&&es.size()<=4096,"Invalid history response points");
        std::vector<double> e,r;
        for(size_t i=0;i<es.size();++i){
            need(es[i].is_number()&&rs[i].is_number(),"History response points must be numeric");
            double x=es[i],y=rs[i];
            need(std::isfinite(x)&&x>0&&(e.empty()||x>e.back())&&std::isfinite(y)&&y>=0,"Invalid history response point");
            e.push_back(x);r.push_back(y);
        }
        const auto offset=size_t(it-species.begin())*stride;
        auto responseAt=[&](double x){
            auto high=std::upper_bound(e.begin(),e.end(),x);
            if(high==e.end())return r.back();
            const auto i=size_t(high-e.begin()-1);
            const double f=(x-e[i])/(e[i+1]-e[i]);
            return (1-f)*r[i]+f*r[i+1];
        };
        for(size_t i=1;i<s.edges.size();++i){
            const double a=s.edges[i-1],b=s.edges[i];
            if(a<e.front()||b>e.back())continue;
            double x=a,y=responseAt(a),average=0;
            auto add=[&](double end,double value){
                average+=(end-x)/(b-a)*(y/2+value/2);x=end;y=value;
            };
            for(auto node=std::upper_bound(e.begin(),e.end(),a);node!=e.end()&&*node<b;++node)
                add(*node,r[size_t(node-e.begin())]);
            add(b,responseAt(b));
            need(std::isfinite(average),"History response coefficient overflow");
            values[offset+i]=average;
        }
    }
    need(std::any_of(values.begin(),values.end(),[](const auto& v){return v.has_value();}),"No fluence bin is fully covered by the response table");
    return values;
}
Json otherDetail(const TargetFluenceSpec& s,const OtherFluenceMoments& detail,
                 const Moments& aggregate,double factor){
    need(detail.size()<=maxOtherFluenceBins,"Too many identified secondary fluence bins");
    Json out=Json::array();
    double mean=0,maxSe=0,sumSquares=0;
    std::int64_t nonzeroSum=0;
    const double n=aggregate.count;
    const auto& standard=fluenceSpecies();
    for(const auto& [key,m]:detail){
        const auto& [name,pdg,bin]=key;
        need(!name.empty()&&name.size()<=160&&std::find(standard.begin(),standard.end(),name)==standard.end()&&
             bin<=s.edges.size(),"Invalid identified secondary fluence bin");
        need(m.count==aggregate.count&&m.nonzero>0&&m.nonzero<=aggregate.nonzero,
             "Identified secondary history counts disagree with other-species total");
        mean+=m.mean;maxSe+=m.standardError();sumSquares+=m.m2+n*m.mean*m.mean;nonzeroSum+=m.nonzero;
        out.push_back({{"particle",name},{"pdg_encoding",pdg},{"bin_index",bin},{"score",row(m,factor)}});
    }
    const double totalSquares=aggregate.m2+n*aggregate.mean*aggregate.mean;
    need(near(mean,aggregate.mean)&&nonzeroSum>=aggregate.nonzero&&std::isfinite(sumSquares)&&std::isfinite(totalSquares)&&
         (sumSquares<=totalSquares||near(sumSquares,totalSquares))&&
         aggregate.standardError()<=maxSe+1e-8*std::max(maxSe,aggregate.mean),
         "Identified secondary fluence does not conserve other-species moments");
    return out;
}
OtherFluenceMoments decodeOtherDetail(const Json& field,int histories,size_t maxBin){
    need(field.is_array()&&field.size()<=maxOtherFluenceBins,"Invalid identified secondary fluence array");
    OtherFluenceMoments detail;
    for(const auto& entry:field){
        need(entry.is_object()&&entry.size()==4&&entry.at("particle").is_string(),"Invalid identified secondary fluence row");
        const auto& pdg=entry.at("pdg_encoding");const auto& bin=entry.at("bin_index");
        const bool validPdg=pdg.is_number_unsigned()?pdg.get<std::uint64_t>()<=std::numeric_limits<int>::max():
            pdg.is_number_integer()&&pdg.get<std::int64_t>()>=std::numeric_limits<int>::min()&&
            pdg.get<std::int64_t>()<=std::numeric_limits<int>::max();
        const bool validBin=bin.is_number_unsigned()?bin.get<std::uint64_t>()<=maxBin:
            bin.is_number_integer()&&bin.get<std::int64_t>()>=0&&bin.get<std::uint64_t>()<=maxBin;
        need(validPdg&&validBin,"Invalid secondary PDG encoding or energy bin");
        OtherFluenceBin key{entry.at("particle").get<std::string>(),pdg.get<int>(),bin.get<size_t>()};
        need(detail.emplace(std::move(key),decode(entry.at("score").at("moments"),histories)).second,
             "Duplicate identified secondary fluence bin");
    }
    return detail;
}
Json responseReport(const TargetFluenceSpec& s,const std::vector<Moments>& bins,const Moments& total,const Moments& response,double factor){
    need(response.count==total.count&&response.nonzero<=total.nonzero,"Invalid history response support");
    auto raw=moment(response);
    decode(raw,total.count);
    double mean=0,maxSe=0,uncovered=0;
    int uncoveredBins=0;
    for(size_t i=0;i<bins.size();++i){
        const auto c=s.responseCoefficients.at(i);
        if(c){mean+=bins[i].mean * (*c);maxSe+=bins[i].standardError() * (*c);}
        else if(bins[i].nonzero){++uncoveredBins;uncovered+=bins[i].mean*factor;}
    }
    const double se=response.standardError();
    need(near(response.mean,mean)&&std::isfinite(maxSe)&&se<=maxSe+1e-8*std::max(maxSe,response.mean),
         "History response moments violate the verified bin means or uncertainty bound");
    const double estimate=response.mean*factor,normalizedSe=se*factor;
    need(std::isfinite(estimate)&&std::isfinite(normalizedSe)&&std::isfinite(uncovered),"History response normalization overflow");
    const bool complete=uncoveredBins==0&&total.nonzero>0;
    const auto& metric=s.responseTable.at("metric");
    return {{"schema","spacerad.target-history-response/1"},{"metric",metric},{"target_id",s.target},
            {"estimator","history_sum_of_bin_averaged_response_times_weighted_track_length"},
            {"result_unit",metric=="ddd"?"MeV_per_g":"expected_events_per_device"},
            {"response_source",s.responseTable.at("source_file")},{"covered_moments",raw},
            {"uncovered_observed_bins",uncoveredBins},{"uncovered_observed_fluence_cm-2",uncovered},
            {"status",uncoveredBins?"incomplete_observed_coverage":!total.nonzero?"no_observed_fluence":!response.nonzero?"no_observed_response":"estimated"},
            {"observed_folded_value",complete?Json(estimate):Json(nullptr)},
            {"standard_error",complete&&response.nonzero?Json(normalizedSe):Json(nullptr)},
            {"scope","Empirical history-level MC uncertainty for the selected binned, angle-averaged response only; excludes unresolved rare events, binning, transport systematics and calibration"}};
}
}
const std::array<std::string,6>& fluenceSpecies(){
    static const std::array<std::string,6> species={"e-","e+","proton","alpha","gamma","neutron"};
    return species;
}
Json TargetFluenceSpec::toJson() const{
    Json out={{"schema","spacerad.target-fluence-settings/1"},{"target_id",target},{"energy_edges_MeV",edges}};
    if(!responseTable.is_null()){out["schema"]="spacerad.target-fluence-settings/2";out["response_table"]=responseTable;}
    if(identifyOtherSpecies)out["schema"]="spacerad.target-fluence-settings/3";
    return out;
}
TargetFluenceSpec TargetFluenceSpec::fromJson(const Json& j){
    need(j.is_object(),"Unsupported target fluence settings");
    const auto& schema=j.at("schema");
    const bool detailed=schema=="spacerad.target-fluence-settings/3";
    const bool response=j.contains("response_table");
    need(j.size()==(response?4:3)&&(detailed||(response?schema=="spacerad.target-fluence-settings/2":
         schema=="spacerad.target-fluence-settings/1"))&&(!response||j.at("response_table").is_object()),
         "Unsupported target fluence settings");
    TargetFluenceSpec s;
    s.identifyOtherSpecies=detailed;
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
    if(response){s.responseTable=j.at("response_table");s.responseCoefficients=coefficients(s);}
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
Json makeTargetFluence(const Project& p,const TargetFluenceSpec& s,const std::string& hash,const std::vector<Moments>& m,const Moments& total,const Moments* response,const OtherFluenceMoments& detail){
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
    Json out={{"schema",s.identifyOtherSpecies?"spacerad.target-fluence/2":"spacerad.target-fluence/1"},{"settings",s.toJson()},{"settings_sha256",hash},
            {"volume_mm3",volume},{"estimator","volume_track_length_pre_step_kinetic_energy"},
            {"bins","underflow, [lower,upper), overflow; other species aggregated separately"},
            {"total",row(total,factor)},{"species",by},{"other_species",row(m.back(),factor)}};
    if(s.identifyOtherSpecies)out["other_species_detail"]=otherDetail(s,detail,m.back(),factor);
    if(response)out["history_response"]=responseReport(s,m,total,*response,factor);
    return out;
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
    std::optional<Moments> response;
    if(!s.responseTable.is_null())response=decode(field.at("history_response").at("covered_moments"),p.events);
    const auto detail=s.identifyOtherSpecies?decodeOtherDetail(field.at("other_species_detail"),p.events,s.edges.size()):OtherFluenceMoments{};
    const auto expected=makeTargetFluence(p,s,hash,moments,decode(field.at("total").at("moments"),p.events),response?&*response:nullptr,detail);
    need(field.size()==expected.size()&&field.at("volume_mm3").is_number()&&near(field.at("volume_mm3"),expected.at("volume_mm3")),"Fluence volume differs from project");
    for(const char* key:{"schema","settings","settings_sha256","estimator","bins"})need(field.at(key)==expected.at(key),"Fluence metadata differs from settings");
    for(const char* key:{"total","other_species"})checkRow(field.at(key),expected.at(key));
    for(const auto& name:fluenceSpecies())for(size_t i=0;i<by.at(name).size();++i)checkRow(by.at(name)[i],expected.at("species").at(name)[i]);
    if(s.identifyOtherSpecies){
        const auto& actual=field.at("other_species_detail");const auto& rows=expected.at("other_species_detail");
        for(size_t i=0;i<rows.size();++i){
            for(const char* key:{"particle","pdg_encoding","bin_index"})
                need(actual[i].at(key)==rows[i].at(key),"Unexpected order of identified secondary fluence bins");
            checkRow(actual[i].at("score"),rows[i].at("score"));
        }
    }
    if(response)checkResponse(field.at("history_response"),expected.at("history_response"));
}
}
