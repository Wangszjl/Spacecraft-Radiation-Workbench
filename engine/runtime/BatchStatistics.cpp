#include "BatchStatistics.h"
#include "VoxelStatistics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Batch: ")+m);}
int integer(const Json& j,const char* key,int low,int high){
    const auto& v=j.at(key);require(v.is_number_integer(),"integer setting required");
    auto n=v.get<std::int64_t>();require(n>=low&&n<=high,"integer setting outside range");return int(n);
}
double real(const Json& j,const char* key,double low,double high){
    const auto& v=j.at(key);require(v.is_number(),"numeric setting required");auto n=v.get<double>();
    require(std::isfinite(n)&&n>=low&&n<=high,"numeric setting outside range");return n;
}
struct Pool {
    std::int64_t n=0,nonzero=0;double mean=0,m2=0;
    void add(const Json& v,int histories){
        int count=integer(v,"count",2,100000000),nz=integer(v,"nonzero_histories",0,count);
        require(count==histories,"moment count differs from completed histories");
        double average=real(v,"mean_MeV",0,1e300),moment=real(v,"m2_MeV2",0,1e300);
        require((nz>0||average==0)&&(average>0||nz==0),"inconsistent zero-deposit moments");
        require(n+count<=100000000,"merged history budget too large");
        auto total=n+count;double delta=average-mean;
        m2+=moment+delta*delta*double(n)*count/double(total);
        mean+=delta*count/double(total);n=total;nonzero+=nz;
        require(std::isfinite(mean)&&std::isfinite(m2),"nonfinite pooled moments");
    }
    Json json() const{return {{"count",n},{"nonzero_histories",nonzero},{"mean_MeV",mean},{"m2_MeV2",m2}};}
    double se() const{return std::sqrt(m2/(double(n)*double(n-1)));}
};
Json comparableProject(Json p){p.erase("events");p.erase("seed");p.erase("provenance");return p;}
}
BatchOptions BatchOptions::fromJson(const Json& j){
    const std::set<std::string> keys={"schema","histories_per_batch","max_histories","max_parallel","max_seconds", "target_relative_error","min_nonzero_histories","min_completed_batches"};
    require(j.is_object()&&j.size()==keys.size(),"missing/extra batch settings");
    for(auto it=j.begin();it!=j.end();++it)require(keys.count(it.key())!=0,"unknown batch setting");
    require(j.at("schema")=="scdat.radiation-batch/1","unknown batch schema");BatchOptions o;
    o.historiesPerBatch=integer(j,"histories_per_batch",2,100000000);o.maxHistories=integer(j,"max_histories",2,100000000);
    o.maxParallel=integer(j,"max_parallel",1,8);o.maxSeconds=real(j,"max_seconds",0,86400);
    o.targetRelativeError=real(j,"target_relative_error",1e-9,1);
    o.minNonzeroHistories=integer(j,"min_nonzero_histories",2,100000000);
    o.minCompletedBatches=integer(j,"min_completed_batches",2,10000);return o;
}
Json BatchOptions::toJson()const{return {{"schema","scdat.radiation-batch/1"},{"histories_per_batch",historiesPerBatch},
    {"max_histories",maxHistories},{"max_parallel",maxParallel},{"max_seconds",maxSeconds},
    {"target_relative_error",targetRelativeError},{"min_nonzero_histories",minNonzeroHistories},{"min_completed_batches",minCompletedBatches}};}
std::vector<BatchEntry> planBatches(const Project& p,const BatchOptions& raw){
    auto o=BatchOptions::fromJson(raw.toJson());p.validate();require(p.events==o.maxHistories,"project events must equal max_histories");
    // A final single event is joined to the preceding batch so every variance has n>=2.
    std::vector<BatchEntry> entries;int first=0;
    while(first<o.maxHistories){
        int n=std::min(o.historiesPerBatch,o.maxHistories-first);if(o.maxHistories-first-n==1)++n;
        require(entries.size()<10000,"at most 10000 batches per job");
        int i=int(entries.size());int seed=int((std::int64_t(p.seed)-1+i)%2147483646)+1;
        entries.push_back({i,seed,first,n});first+=n;
    }return entries;
}
Json batchPlanJson(const std::vector<BatchEntry>& entries){Json j=Json::array();for(const auto& e:entries)j.push_back({
    {"index",e.index},{"seed",e.seed},{"event_begin",e.firstEvent},{"event_end_exclusive",e.firstEvent+e.histories},{"histories",e.histories}});return j;}
Json mergeBatchResults(const Project& project,const std::vector<Json>& results){
    require(!results.empty(),"no completed batch to merge");
    Json out=results.front();out["targets"]=Json::array();out["score_moments"]=Json::object();out["track_segments"]=Json::array();
    std::map<std::string,std::array<Pool,3>> pools;std::map<std::string,Json> templates;
    std::map<std::string,std::int64_t> entries;std::set<int> seeds;std::int64_t total=0;
    auto expected=project.expectedPrimaries();
    for(const auto& r:results){
        require(r.at("status")=="completed"&&r.at("schema")=="scdat.radiation-result/2","batch is not a completed supported result");
        int n=integer(r,"histories",2,100000000);int seed=integer(r.at("project"),"seed",1,2147483646);
        require(seeds.insert(seed).second,"duplicate batch seed");
        require(integer(r.at("project"),"events",2,100000000)==n,"project history mismatch");
        require(comparableProject(r.at("project"))==comparableProject(project.toJson()),"batch mission/geometry configuration differs");
        require(r.at("geometry_audit")==results.front().at("geometry_audit")&&r.at("material_audit")==results.front().at("material_audit")&&
            r.at("runtime_audit")==results.front().at("runtime_audit"),"batch geometry/material/runtime mismatch");
        require(real(r,"expected_primaries",0,1e300)==expected,"batch exposure differs");
        require(r.at("score_moments").is_object()&&r.at("targets").is_array()&&!r.at("targets").empty(),"missing batch target moments");
        std::set<std::string> ids;
        for(const auto& t:r.at("targets")){
            auto id=t.at("id").get<std::string>();require(ids.insert(id).second,"duplicate target ID");
            require(r.at("score_moments").contains(id),"target moments missing");
            if(!templates.count(id)){require(total==0,"batch target set differs");templates[id]=t;}
            require(t.at("mass_kg")==templates.at(id).at("mass_kg"),"target mass differs");real(t,"mass_kg",1e-300,1e300);
            entries[id]+=integer(t,"primary_entry_histories",0,n);
            auto& v=pools[id];const auto& m=r.at("score_moments").at(id);
            v[0].add(m.at("ionizing"),n);v[1].add(m.at("total"),n);v[2].add(m.at("nonionizing"),n);
        }
        require(ids.size()==templates.size()&&ids.size()==r.at("score_moments").size(),"batch target set differs");
        total+=n;require(total<=project.events,"completed histories exceed project budget");
    }
    for(const auto& pair:pools){const auto& id=pair.first;const auto& v=pair.second;
        double factor=1.602176634e-13/templates.at(id).at("mass_kg").get<double>()*expected;
        double dose=v[0].mean*factor,error=v[0].se()*factor;require(std::isfinite(dose)&&std::isfinite(error),"nonfinite dose");
        Json t={{"id",id},{"mass_kg",templates.at(id).at("mass_kg")},{"nonzero_histories",v[0].nonzero},
            {"tid_Gy",dose},{"tid_rad",dose*100},{"tid_standard_error_Gy",error},
            {"total_absorbed_Gy",v[1].mean*factor},{"nonionizing_Gy",v[2].mean*factor},
            {"relative_standard_error",nullptr},{"primary_entry_histories",entries.at(id)}};
        if(v[0].mean>0)t["relative_standard_error"]=v[0].se()/v[0].mean;
        out["targets"].push_back(t);out["score_moments"][id]={{"ionizing",v[0].json()},{"total",v[1].json()},{"nonionizing",v[2].json()}};
    }
    out["histories"]=total;out["expected_primaries"]=expected;
    mergeVoxelResults(project,results,out);
    out["source_diagnostics"]={{"histories",total},{"scope","Per-batch source diagnostics retained in child results; not pooled here"}};
    // A bounded preview from the first completed batch; global identity is explicit.
    for(auto segment:results.front().at("track_segments")){segment["batch_index"]=0;out["track_segments"].push_back(std::move(segment));}
    return out;
}
Json batchConvergence(const Json& merged,const BatchOptions& o,int completed){
    Json targets=Json::array();bool all=completed>=o.minCompletedBatches;
    if(!merged.contains("targets")||merged.at("targets").empty())all=false;
    else for(const auto& t:merged.at("targets")){
        bool sufficient=t.at("nonzero_histories").get<std::int64_t>()>=o.minNonzeroHistories;
        bool ok=sufficient&&!t.at("relative_standard_error").is_null()&&t.at("relative_standard_error").get<double>()<=o.targetRelativeError&&completed>=o.minCompletedBatches;
        targets.push_back({{"id",t.at("id")},{"converged",ok},{"relative_standard_error",t.at("relative_standard_error")},
            {"nonzero_histories",t.at("nonzero_histories")}});all=all&&ok;
    }
    return {{"converged",all},{"targets",targets},{"target_relative_error",o.targetRelativeError},
        {"min_nonzero_histories",o.minNonzeroHistories},{"min_completed_batches",o.minCompletedBatches},
        {"scope","MC statistical stopping only; zero hits are not a risk upper bound or external physics validation"}};
}
}
