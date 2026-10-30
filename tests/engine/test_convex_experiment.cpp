#include "runtime/MeshExperimentJob.h"
#include "runtime/ConvexSeparation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
QByteArray bytes(const Json& j){return QByteArray::fromStdString(j.dump());}
MeshExperimentJob parse(const Json& j,bool blocked=false){return parseMeshExperiment(bytes(j),blocked);}
bool rejects(const Json& j){try{(void)parse(j);return false;}catch(const std::exception&){return true;}}
Json box(std::string id,std::string material,bool sensitive,Vector3 center={0,0,0},Vector3 rotation={0,0,0},double scale=1){
    TriangleMesh m{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
      {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}}};
    for(auto& p:m.points)for(auto& x:p)x*=scale;
    return makeTriangleMeshBundle(m,id,material,sensitive,center,rotation,{{"kind","external_mesh"},{"description","synthetic convex contract fixture"}});
}
}
int main(){try{
    auto demo=meshRotatedExperimentDemo("e-");
    check(demo.at("schema")=="scdat.mesh-experiment/3","v3 schema");
    check(demo.at("separation_contract")=="scdat.convex-separation/1","v3 contract");
    auto job=parse(demo);
    check(job.convexSeparation&&job.assembly&&job.parts.size()==3,"v3 demo parses as three-part convex assembly");
    check(job.source.spectra.size()==1&&std::abs(job.source.integratedSpectrum()-1e7)<1e-7,"electron fluence normalized");
    check(parse(meshRotatedExperimentDemo("proton")).source.spectra[0].sample(.5)==15,"proton source retained");
    const auto a=job.parts[0].report.at("world_bounds_mm").get<std::array<double,6>>();
    const auto b=job.parts[1].report.at("world_bounds_mm").get<std::array<double,6>>();
    bool aabbOverlap=true;for(int d=0;d<3;++d)aabbOverlap&=a[2*d]<=b[2*d+1]&&b[2*d]<=a[2*d+1];
    check(aabbOverlap,"sensitive-body world AABBs overlap");
    check(job.separationAudit.at("schema")=="scdat.convex-separation/1"&&job.separationAudit.at("complete")==true&&job.separationAudit.at("separation_gate_passed")==true,"full separation audit passes");
    check(job.separationAudit.at("pairs").size()==3,"all three body pairs audited");
    for(const auto& row:job.separationAudit.at("pairs")){check(row.at("status")=="separated","each pair has separation witness");check(row.at("witness").at("gap_lower_bound_mm").get<double>()>1e-4,"strict witness gap");}
    check(job.sourceEnclosure.at("passed")==true,"source enclosure passes");
    check(job.source.volumes.size()==3,"validation source has three proxies");
    for(size_t i=0;i<3;++i){check(job.source.volumes[i].id==job.parts[i].report.at("id"),"proxy ID matches real mesh");check(job.source.volumes[i].material==job.parts[i].report.at("material"),"proxy material matches real mesh");check(job.source.volumes[i].sensitive==job.parts[i].report.at("sensitive"),"proxy sensitivity matches real mesh");}

    auto bad=demo;bad.erase("separation_contract");check(rejects(bad),"v3 missing contract rejected");bad=demo;bad["separation_contract"]="wrong";check(rejects(bad),"v3 wrong contract rejected");
    bad=meshAssemblyExperimentDemo();bad["separation_contract"]="scdat.convex-separation/1";check(rejects(bad),"v2 rejects new contract field");
    bad=demo;bad["schema"]="scdat.mesh-experiment/2";bad.erase("separation_contract");check(rejects(bad),"rotated demo downgraded to v2 remains rejected");
    bad=demo;bad["parts"][1]["part"]["id"]=bad["parts"][0]["part"]["id"];check(rejects(bad),"v3 duplicate IDs rejected");
    bad=demo;bad["parts"][0]["part"]["id"]="World";check(rejects(bad),"v3 reserved World ID rejected");
    bad=demo;bad["parts"][0]["part"]["triangles"].erase(0);check(rejects(bad),"v3 open part rejected");
    bad=demo;for(auto& part:bad["parts"])part["part"]["sensitive"]=false;check(rejects(bad),"v3 all-nonsensitive assembly rejected");
    bad=demo;bad["run"]["source_radius_mm"]=1;check(rejects(bad),"v3 undersized source rejected");
    bad=demo;for(auto& part:bad["parts"])part["part"]["center"][0]=part["part"]["center"][0].get<double>()+100.0;
    check(rejects(bad),"v3 actual meshes outside source sphere rejected despite central proxy scaffold");

    auto overlap=demo;overlap["parts"][0]=box("A","G4_Si",true);overlap["parts"][1]=box("B","G4_Si",true);overlap["parts"][2]=box("C","G4_Al",false,{0,0,5});
    check(rejects(overlap),"overlapping bodies rejected by default");auto inspected=parse(overlap,true);check(inspected.separationAudit.at("separation_gate_passed")==false&&inspected.separationAudit.at("transport_allowed")==false,"blocked inspection cannot authorize transport");
    auto contact=overlap;contact["parts"][1]=box("B","G4_Si",true,{2,0,0});check(rejects(contact),"contact rejected");
    auto near=overlap;near["parts"][1]=box("B","G4_Si",true,{2.00005,0,0});check(rejects(near),"sub-threshold gap rejected");

    auto v2=meshAssemblyExperimentDemo("e-",false);v2["parts"]={box("same","G4_Si",true),box("same","G4_Al",false,{5,0,0})};check(rejects(v2),"duplicate IDs rejected");
    v2["parts"]={box("World","G4_Si",true),box("B","G4_Al",false,{5,0,0})};check(rejects(v2),"reserved World ID rejected");
    v2["parts"]={box("A","G4_Si",false),box("B","G4_Al",false,{5,0,0})};check(rejects(v2),"all-passive assembly rejected");
    v2["parts"]={box("A","G4_Si",true),box("B","G4_Al",false,{5,0,0})};v2["parts"][0]["part"]["triangles"].erase(0);check(rejects(v2),"open mesh rejected");
    v2["parts"]={box("A","G4_Si",true),box("B","G4_Al",false,{5,0,0})};v2["run"]["source_radius_mm"]=1;check(rejects(v2),"undersized source rejected");
    v2["parts"]={box("A","G4_Si",true),box("B","G4_Al",false,{5,0,0})};
    auto concave=v2;concave["parts"][0]["part"]["points"][6]={0,0,0};check(rejects(concave),"concave/non-convex mesh rejected");

    auto parsed=parse(meshRotatedExperimentDemo());ConvexSeparationBudget budget;budget.axesPerPair=0;
    auto exhausted=auditConvexSeparation(parsed.parts,budget);check(!exhausted.at("complete").get<bool>()&&!exhausted.at("separation_gate_passed").get<bool>(),"axis budget exhaustion blocks gate");
    budget=ConvexSeparationBudget{};budget.vertexProjections=0;exhausted=auditConvexSeparation(parsed.parts,budget);check(!exhausted.at("complete").get<bool>()&&!exhausted.at("separation_gate_passed").get<bool>(),"projection budget exhaustion blocks gate");
    budget=ConvexSeparationBudget{};budget.candidatesPerPair=0;exhausted=auditConvexSeparation(parsed.parts,budget);check(!exhausted.at("complete").get<bool>()&&!exhausted.at("separation_gate_passed").get<bool>(),"candidate budget exhaustion blocks gate");
#if defined(_M_X64) || defined(__SSE2__)
    auto saved=_mm_getcsr();auto unusual=saved|0xe040u;_mm_setcsr(unusual);auto scoped=auditConvexSeparation(parsed.parts);auto restored=_mm_getcsr()==unusual;_mm_setcsr(saved);check(restored&&scoped.at("separation_gate_passed")==true,"interval scope handles and restores caller FP rounding/flush state");
#endif
    std::cout<<checks<<" convex experiment checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
