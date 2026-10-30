#include "runtime/MeshExperimentJob.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace spacerad::engine;

namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
MeshExperimentJob parse(const Json& j){return parseMeshExperiment(QByteArray::fromStdString(j.dump()));}
bool rejects(const Json& j){try{(void)parse(j);return false;}catch(const std::exception&){return true;}}
Json single(const std::string& id,const std::string& material,bool sensitive,Vector3 center={0,0,0},Vector3 rotation={0,0,0},double scale=1.0){
    TriangleMesh box{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}}};
    for(auto& p:box.points)for(double& coordinate:p)coordinate*=scale;
    return makeTriangleMeshBundle(box,id,material,sensitive,center,rotation,{{"kind","external_mesh"},{"description","synthetic assembly test"}});
}
Json pairJob(double secondX=3.0){
    auto base=meshAssemblyExperimentDemo("e-",false);
    base["parts"]={single("SiA","G4_Si",true,{0,0,0}),single("SiB","G4_Si",false,{secondX,0,0})};
    return base;
}
}

int main(){try{
    auto demo=meshAssemblyExperimentDemo();
    check(demo.at("schema")=="scdat.mesh-experiment/2","v2 schema");
    check(demo.at("solid")=="tessellated","demo solid type");
    auto assembled=parse(demo);
    check(assembled.assembly,"assembly flag");
    check(assembled.parts.size()==8,"shielded demo has eight bodies");
    check(assembled.source.volumes.size()==8,"source scaffold covers every body");
    check(assembled.separationAudit.is_object(),"separation audit retained");
    check(assembled.separationAudit.at("complete")==true,"pair audit is complete");
    check(assembled.separationAudit.at("pair_count")==28,"eight bodies produce all 28 audited pairs");
    bool sawSensitive=false,sawPassive=false,sawAl=false,sawSi=false;
    std::set<std::string> ids;
    for(size_t i=0;i<assembled.parts.size();++i){const auto& part=assembled.parts[i];
        check(part.report.at("geometry_gate_passed")==true,"demo part passes v1 closed convex gate");
        auto id=part.report.at("id").get<std::string>();check(ids.insert(id).second,"unique part identity");
        const auto material=part.report.at("material").get<std::string>();
        check(assembled.source.volumes[i].id==id,"scoring scaffold preserves part ID");
        check(assembled.source.volumes[i].material==material,"scoring scaffold preserves material");
        sawAl|=material=="G4_Al";sawSi|=material=="G4_Si";
        const bool sensitive=part.report.at("sensitive").get<bool>();check(assembled.source.volumes[i].sensitive==sensitive,"scoring scaffold preserves sensitivity");sawSensitive|=sensitive;sawPassive|=!sensitive;
    }
    check(sawAl&&sawSi,"demo contains aluminum shield and silicon detector");
    check(sawSensitive&&sawPassive,"assembly supports sensitive and passive bodies");
    check(assembled.source.synthetic&&assembled.source.spectra.size()==1,"v2 source normalized through v1 contract");
    check(std::abs(assembled.source.integratedSpectrum()-1e7)<1e-7,"v2 fluence normalization");
    auto bare=parse(meshAssemblyExperimentDemo("e-",false));
    check(bare.parts.size()==2&&bare.source.volumes.size()==2,"unshielded demo has two detector bodies");
    check(parse(meshAssemblyExperimentDemo("proton",false)).source.spectra[0].sample(.5)==15,"particle source preserved");

    auto j=pairJob();
    check(!rejects(j),"separated two-part assembly accepted");
    check(rejects(pairJob(1.0)),"overlapping parts rejected");
    check(rejects(pairJob(2.0)),"touching parts rejected");
    check(rejects(pairJob(2.00005)),"sub-threshold gap rejected");
    j=pairJob();j["parts"][0]=single("Outer","G4_Si",true,{0,0,0});j["parts"][1]=single("Inner","G4_Si",false,{0,0,0},{0,0,0},.25);check(rejects(j),"nested body rejected");
    j=pairJob();j["parts"][0]=single("RotA","G4_Si",true,{0,0,0},{0,0,45},.25);j["parts"][1]=single("RotB","G4_Si",false,{.55,.55,0},{0,0,45},.25);check(rejects(j),"overlapping world AABBs conservatively rejected");
    j=pairJob();j["parts"][1]["part"]["id"]=j["parts"][0]["part"]["id"];check(rejects(j),"duplicate part IDs rejected");
    j=pairJob();j["parts"][0]["part"]["id"]="World";check(rejects(j),"reserved World ID rejected");
    j=pairJob();j["parts"][0]["part"]["sensitive"]=false;j["parts"][1]["part"]["sensitive"]=false;check(rejects(j),"all-passive assembly rejected");
    j=pairJob();j["parts"].erase(1);check(rejects(j),"single-body assembly rejected");
    j=meshAssemblyExperimentDemo();for(int i=0;i<25;++i)j["parts"].push_back(j["parts"][0]);check(rejects(j),"more than 32 bodies rejected");
    j=pairJob();j["schema"]="scdat.mesh-experiment/1";j["solid"]="tessellated";j["mesh"]=single("legacy","G4_Si",true);check(rejects(j),"v1 parser rejects parts field");
    j=pairJob();j["mesh"]=single("legacy","G4_Si",true);check(rejects(j),"v2 parser rejects legacy mesh field");
    j=pairJob();j["parts"][0]["schema"]="scdat.radiation-triangle-mesh/2";check(rejects(j),"v2 parser rejects non-v1 part bundle");
    j=pairJob();j["parts"][0]["part"]["triangles"].erase(0);check(rejects(j),"invalid open part rejected");
    j=pairJob();j["solid"]="analytic_box_reference";check(!rejects(j),"analytic reference accepts exact box parts");
    TriangleMesh tetra{{{0,0,0},{2,0,0},{0,2,0},{0,0,2}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}}};
    j=pairJob();j["solid"]="analytic_box_reference";j["parts"][0]=makeTriangleMeshBundle(tetra,"Tetra","G4_Si",true,{-3,0,0},{0,0,0},{{"kind","external_mesh"},{"description","synthetic tetra"}});check(rejects(j),"analytic box reference rejects tetrahedron");
    std::cout<<checks<<" mesh assembly checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
