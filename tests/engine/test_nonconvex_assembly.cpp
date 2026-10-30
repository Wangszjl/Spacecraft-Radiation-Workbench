#include "runtime/NonconvexAssembly.h"
#include <QCryptographicHash>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
QByteArray bytes(const Json& j){return QByteArray::fromStdString(j.dump());}
MeshExperimentJob parse(const Json& j,bool blocked=false){return parseMeshExperiment(bytes(j),blocked);}
bool rejects(const Json& j,bool blocked=false){try{(void)parse(j,blocked);return false;}catch(const std::exception&){return true;}}
std::string digest(const Json& j){return QCryptographicHash::hash(bytes(j),QCryptographicHash::Sha256).toHex().toStdString();}
void rehash(Json& j,size_t i){j["origins"][i]["mesh_sha256"]=digest(j["parts"][i]);}
}
int main(){try{
    auto demo=nonconvexAssemblyDemo();
    check(demo.at("schema")=="scdat.mesh-experiment/5"&&demo.at("separation_contract")=="scdat.vertex-hull-separation/1","v5 vertex hull contract");
    check(demo.at("solid")=="tessellated","tessellated demo solid");
    auto job=parse(demo);
    check(job.nonconvexAssembly&&job.assembly&&job.parts.size()==4,"v5 parsed as four-part nonconvex assembly");
    check(job.referenceProfiles.empty(),"tessellated demo has no analytic reference profiles");
    check(job.source.volumes.size()==4,"source scaffold covers all bodies");
    check(job.separationAudit.at("schema")=="scdat.vertex-hull-separation/1"&&job.separationAudit.at("complete")==true&&job.separationAudit.at("separation_gate_passed")==true,"complete hull separation passes");
    check(job.separationAudit.at("pairs").size()==6,"all six pairs audited");
    for(const auto& p:job.parts)check(p.report.at("geometry_gate_passed")==true,"closed oriented positive-volume concave or box mesh passes per-part gate");
    for(const auto& p:job.separationAudit.at("pairs")){check(p.at("status")=="separated"&&p.at("witness").at("gap_lower_bound_mm").get<double>()>1e-4,"pair has strict positive hull gap");}
    check(std::abs(job.parts[0].report.at("quality").at("signed_volume_mm3").get<double>()-1200.0)<1e-9&&std::abs(job.parts[1].report.at("quality").at("signed_volume_mm3").get<double>()-1200.0)<1e-9,"L prisms preserve expected volume");
    check(job.source.volumes[2].sensitive&&job.source.volumes[3].sensitive,"two silicon bodies are sensitive");

    auto blocked=demo;auto b0=blocked["parts"][0]["part"]["center"].get<std::array<double,3>>();
    blocked["parts"][1]["part"]["center"]={b0[0],b0[1],b0[2]};rehash(blocked,1);
    check(rejects(blocked),"overlapping hull assembly rejected by default");
    auto inspected=parse(blocked,true);check(!inspected.separationAudit.at("separation_gate_passed").get<bool>()&&!inspected.separationAudit.at("transport_allowed").get<bool>(),"blocked inspection exposes failed audit but cannot authorize transport");

    auto bad=demo;bad["schema"]="scdat.mesh-experiment/3";bad.erase("origins");bad["separation_contract"]="scdat.convex-separation/1";check(rejects(bad),"v3 downgrade cannot authorize concave mesh");
    bad=demo;bad["schema"]="scdat.mesh-experiment/2";check(rejects(bad),"v2 rejects v5 fields");
    bad=demo;bad["solid"]="orthogonal_l_reference";check(!rejects(bad),"CLI orthogonal L exact reference fixture accepted");auto referenceJob=parse(bad);check(referenceJob.referenceProfiles.size()==4&&referenceJob.referenceProfiles[0]=="orthogonal_l"&&referenceJob.referenceProfiles[1]=="orthogonal_l"&&referenceJob.referenceProfiles[2]=="box"&&referenceJob.referenceProfiles[3]=="box","exact reference profiles derived only for reference solid");
    bad=demo;bad["solid"]="orthogonal_l_reference";bad["parts"][0]["part"]["points"][0][0]=bad["parts"][0]["part"]["points"][0][0].get<double>()+.1;rehash(bad,0);check(rejects(bad),"reference profile cannot stand in for modified arbitrary surface");
    bad=demo;bad["solid"]="analytic_box_reference";check(rejects(bad),"v5 rejects legacy analytic box reference");

    bad=demo;bad.erase("origins");check(rejects(bad),"missing origins rejected");
    bad=demo;bad["origins"][0].erase("mesh_sha256");check(rejects(bad),"missing origin digest rejected");
    bad=demo;bad["origins"][0].erase("part_id");check(rejects(bad),"missing origin part ID rejected");
    bad=demo;bad["origins"][0]["extra"]=true;check(rejects(bad),"extra origin fields rejected");
    bad=demo;bad["origins"][0]["kind"]="source_beam";check(rejects(bad),"non-synthetic beam origin rejected");
    bad=demo;bad["origins"].push_back(bad["origins"][0]);check(rejects(bad),"extra origin rejected");
    bad=demo;bad["separation_contract"]="wrong";check(rejects(bad),"wrong separation contract rejected");
    bad=demo;while(bad["parts"].size()>1){bad["parts"].erase(bad["parts"].size()-1);bad["origins"].erase(bad["origins"].size()-1);}check(rejects(bad),"single part rejected");
    bad=demo;for(int i=0;i<13;++i){auto p=bad["parts"][0];p["part"]["id"]="Extra_"+std::to_string(i);bad["parts"].push_back(p);auto o=bad["origins"][0];o["part_id"]=p["part"]["id"];o["mesh_sha256"]=digest(p);bad["origins"].push_back(o);}check(rejects(bad),"more than sixteen bodies rejected");
    bad=demo;while(bad["parts"][0]["part"]["points"].size()<=4096)bad["parts"][0]["part"]["points"].push_back(bad["parts"][0]["part"]["points"][0]);rehash(bad,0);check(rejects(bad),"over-budget point count rejected");
    bad=demo;bad["parts"][0]["part"]["id"]="Other";check(rejects(bad),"origin ID mismatch rejected");
    bad=demo;for(size_t i=0;i<bad["parts"].size();++i){bad["parts"][i]["part"]["sensitive"]=false;rehash(bad,i);}check(rejects(bad),"all-insensitive assembly rejected");
    bad=demo;bad["parts"][2]["part"]["center"][0]=-1;rehash(bad,2);check(rejects(bad),"touching sensitive boxes rejected");
    bad=demo;bad["parts"][2]["part"]["center"][0]=3;rehash(bad,2);check(rejects(bad),"overlapping bodies rejected");
    bad=demo;bad["run"]["source_radius_mm"]=1;check(rejects(bad),"undersized source rejected");
    bad=demo;bad["source"]["synthetic"]=false;check(rejects(bad),"non-synthetic source beam rejected");
    bad=demo;bad["origins"][0]["description"]="";check(rejects(bad),"empty synthetic origin description rejected");
    bad=demo;bad["parts"][0]["part"]["triangles"].erase(0);rehash(bad,0);check(rejects(bad),"open mesh rejected");
    bad=demo;bad["parts"][0]["part"]["points"][0][0]=std::numeric_limits<double>::quiet_NaN();rehash(bad,0);check(rejects(bad),"nonfinite coordinates rejected");
    std::cout<<checks<<" nonconvex assembly checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
