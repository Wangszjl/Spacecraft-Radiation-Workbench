#include "runtime/M1Audit.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(F f){bool ok=false;try{f();}catch(const std::exception&){ok=true;}check(ok,"invalid M1 plan accepted");}
bool gate(const Json& a,const std::string& id){for(const auto& g:a.at("gates"))if(g.at("id")==id)return g.at("passed").get<bool>();throw std::runtime_error("missing gate: "+id);}
Project base(){auto p=Project::demo();p.name="Synthetic M1 math fixture";p.synthetic=true;p.events=1000;p.radius=200;p.cut=.1;p.assemblies.clear();p.templates.clear();p.instances.clear();p.materialSnapshot=nullptr;p.voxelScoring=nullptr;
    p.volumes={{"Shell","G4_Al",{0,0,0},{120,100,80},2,false},{"Target_A","G4_Si",{-20,0,0},{10,10,10},0,true},{"Target_B","G4_Si",{20,0,0},{10,10,10},0,true}};p.validate();return p;}
std::vector<M1Run> runs(const Project& p){std::vector<M1Run> out;for(int i=0;i<4;++i){auto v=p;v.seed+=i*100;if(i==1)v.cut=.05;if(i==2)v.volumes[0].wall=1.8;if(i==3)v.radius=220;v.validate();Json targets=Json::array();for(const auto& id:{"Target_A","Target_B"}){double d=std::string(id)=="Target_A"?1:2;if(i)d*=1+.01*i;targets.push_back({{"id",id},{"mass_kg",.00233},{"tid_Gy",d},{"tid_standard_error_Gy",d*.01},{"relative_standard_error",.01},{"nonzero_histories",1000}});}
    Json r={{"schema","scdat.radiation-result/2"},{"status","completed"},{"histories",1000},{"project",v.toJson()},{"expected_primaries",v.expectedPrimaries()},{"targets",targets},{"worker",{{"sha256",std::string(64,'a')}}},{"runtime_audit",{{"fixture","runtime"}}}};out.push_back({v,r,{{"replay",{{"eligible",true}}}}});}return out;}
Json plan(const Project& p){auto j=draftM1Plan(p,"baseline-job");for(const auto& c:std::vector<std::pair<std::string,std::string>>{{"cut","physics"},{"shell","geometry"},{"radius","source"}})j["sensitivity"].push_back({{"id",c.first},{"category",c.second},{"job_directory",c.first+"-job"},{"max_relative_change",.5}});return validateM1Plan(j);}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{auto p=base();auto j=plan(p);auto r=runs(p);auto a=evaluateM1(j,r,Json::object());
    check(a.at("readiness")=="incomplete"&&!a.at("m1_accepted").get<bool>(),"synthetic case must not accept M1");
    check(gate(a,"multiple_targets")&&gate(a,"target_coverage")&&gate(a,"baseline_complete")&&gate(a,"baseline_statistics"),"complete two-target statistics failed");
    check(!gate(a,"environment_non_synthetic")&&!gate(a,"environment_review_declared")&&!gate(a,"environment_files_bound"),"missing provenance incorrectly passed");
    check(gate(a,"sensitivity_physics")&&gate(a,"sensitivity_geometry")&&gate(a,"sensitivity_source"),"valid category coverage failed");
    check(a.at("sensitivity_checks").size()==6,"need every target for every sensitivity case");
    const auto& c=a.at("sensitivity_checks")[0];double reference=.01+2*std::sqrt(.01*.01+.0101*.0101);
    check(std::abs(c.at("screening_envelope").get<double>()-reference)<1e-12,"independent engineering envelope differs");
    auto same=r;same[1].project.seed=same[0].project.seed;same[1].result["project"]=same[1].project.toJson();check(!gate(evaluateM1(j,same,Json::object()),"distinct_seeds"),"overlapping seed accepted");
    auto wrong=r;wrong[1].project.radius=230;wrong[1].result["project"]=wrong[1].project.toJson();wrong[1].result["expected_primaries"]=wrong[1].project.expectedPrimaries();check(!gate(evaluateM1(j,wrong,Json::object()),"sensitivity_physics"),"unisolated sensitivity accepted");
    auto unknown=r;unknown[0].result["targets"][0]["relative_standard_error"]=nullptr;unknown[0].result["targets"][0]["nonzero_histories"]=0;unknown[0].result["targets"][0]["tid_Gy"]=0;check(!gate(evaluateM1(j,unknown,Json::object()),"baseline_statistics"),"zero hits marked sufficient");
    auto loose=j;loose["sensitivity"][0]["max_relative_change"]=.001;check(!gate(evaluateM1(loose,r,Json::object()),"sensitivity_physics"),"failed threshold accepted");
    auto over=j;over["targets"][0]["max_histories"]=500;check(!gate(evaluateM1(over,r,Json::object()),"baseline_statistics"),"requested/actual budget ignored");
    auto missing=j;missing["targets"].erase(1);check(!gate(evaluateM1(missing,r,Json::object()),"target_coverage"),"unlisted sensitive target ignored");
    auto runtime=r;runtime[1].result["runtime_audit"]["fixture"]="other";check(!gate(evaluateM1(j,runtime,Json::object()),"same_runtime"),"mixed runtime accepted");
    for(const auto& bad:std::vector<Json>{[&]{auto x=j;x["unknown"]=1;return x;}(),[&]{auto x=j;x["targets"][0]["max_histories"]=true;return x;}(),[&]{auto x=j;x["targets"].push_back(x["targets"][0]);return x;}(),[&]{auto x=j;x["sensitivity"][0]["category"]="unknown";return x;}(),[&]{auto x=j;x["sensitivity"][0]["max_relative_change"]=-1;return x;}()})rejects([&]{validateM1Plan(bad);});
    p.synthetic=false;for(auto it=p.environmentMetadata.begin();it!=p.environmentMetadata.end();++it){if(it.key()=="percentile"||it.key()=="confidence_level")it.value()=95;else if(it.key().find("_utc")!=std::string::npos)it.value()="2026-10-21T00:00:00Z";else it.value()="declared fixture";}
    for(auto& s:p.spectra)s.provenance["source_file"]={{"file_name","spectrum.dat"},{"size_bytes",3},{"sha256",std::string(64,'1')}};
    p.validate();r=runs(p);j=plan(p);Json files=Json::object();for(const auto& key:{"environment","geometry","physics","source"}){std::string k=key;Json list=Json::array();if(k=="environment"){list.push_back({{"path","spectrum"},{"role","spectrum"}});list.push_back({{"path","model"},{"role","model_report"}});}else list.push_back({{"path",k},{"role",k+"_review"}});j["reviews"][k]={{"reviewer","declared fixture reviewer"},{"reviewed_utc","2026-10-21T00:00:00Z"},{"notes","Unit fixture; this is not a real approval"},{"files",list}};}
    for(const auto& key:{"spectrum","model","geometry","physics","source"})files[key]={{"sha256",std::string(64,std::string(key)=="spectrum"?'1':'2')},{"size_bytes",std::string(key)=="spectrum"?3:4}};
    for(auto& run:r){run.report["summary"]={{"statistical_state","unconverged"}};run.report["statistics"]={{"criterion",{{"target_relative_error",1e-9}}}};}
    a=evaluateM1(j,r,files);check(a.at("readiness")=="ready_for_review"&&a.at("m1_accepted")==false,"complete declarations must only be ready for review");check(a.at("run_summaries")[0].at("original_statistical_state")=="unconverged"&&a.contains("statistics_policy"),"original stopping state must remain explicit and separate");
    auto legacy=r;for(auto& run:legacy){run.result.erase("worker");run.report["replay"]["eligible"]=false;}check(!gate(evaluateM1(j,legacy,files),"same_runtime"),"missing historical Worker must block readiness");
    files["spectrum"]["sha256"]=std::string(64,'f');check(!gate(evaluateM1(j,r,files),"environment_files_bound"),"source file SHA ignored");
    auto bad=j;bad["reviews"]["geometry"]["reviewed_utc"]="2026-02-30T00:00:00Z";rejects([&]{validateM1Plan(bad);});
    std::cout<<"M1 audit: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
