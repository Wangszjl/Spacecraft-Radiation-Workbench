#include "runtime/MeshExperimentJob.h"
#include <QCryptographicHash>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace spacerad::engine;
int count=0;
void check(bool b,const char* m){++count;if(!b)throw std::runtime_error(m);}
MeshExperimentJob parse(const Json& j){return parseMeshExperiment(QByteArray::fromStdString(j.dump()));}
bool rejects(const Json& j){try{parse(j);return false;}catch(const std::exception&){return true;}}
void refreshEmbeddedHash(Json& j){j["cad_provenance"]["embedded_mesh_sha256"]=QCryptographicHash::hash(QByteArray::fromStdString(j["mesh"].dump()),QCryptographicHash::Sha256).toHex().toStdString();}

// A 2 x 2 L-shaped prism. The four cap triangles cover the concave polygon
// without introducing a diagonal across its missing upper-right quadrant.
TriangleMesh lPrism(){
    const std::array<std::array<double,2>,6> xy={{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}}};
    TriangleMesh m;
    for(double z:{-1.,1.})for(auto p:xy)m.points.push_back({p[0],p[1],z});
    const std::array<std::array<std::size_t,3>,4> cap={{{0,1,3},{1,2,3},{0,3,5},{3,4,5}}};
    for(auto t:cap)m.triangles.push_back({t[0],t[2],t[1]}); // bottom, outward -Z
    for(auto t:cap)m.triangles.push_back({t[0]+6,t[1]+6,t[2]+6}); // top, outward +Z
    for(std::size_t i=0;i<6;++i){std::size_t k=(i+1)%6,b=i,n=k,t=i+6,tn=k+6;
        m.triangles.push_back({b,n,tn});m.triangles.push_back({b,tn,t});}
    return m;
}
Json v4Job(const TriangleMesh& mesh=lPrism()){
    Json j=meshExperimentDemo();j["schema"]="scdat.mesh-experiment/4";
    j["mesh"]=makeTriangleMeshBundle(mesh,"L_Prism","G4_Si",true,{0,0,0},{0,0,0},{{"kind","external_mesh"},{"description","synthetic concave-prism test fixture"}});
    auto report=inspectTriangleMeshBytes(QByteArray::fromStdString(j["mesh"].dump()),"fixture.json",true).report;
    auto hash=[](const std::string& s){return QCryptographicHash::hash(QByteArray::fromStdString(s),QCryptographicHash::Sha256).toHex().toStdString();};
    const std::string digest=hash(j["mesh"].dump());
    j["cad_provenance"]={{"schema","scdat.step-mesh-provenance/1"},
        {"conversion_sha256",std::string(64,'a')},{"source_sha256",std::string(64,'b')},
        {"inventory_sha256",std::string(64,'c')},{"material_map_sha256",std::string(64,'d')},
        {"original_mesh_sha256",std::string(64,'e')},{"embedded_mesh_sha256",digest},
        {"part_id","L_Prism"},{"material","G4_Si"},{"original_sensitive",true},
        {"scoring_override","whole_body_dosimeter"},{"source_solid_count",1},
        {"reference_status","numerically_qualified"},
        {"reference_volume_mm3",report.at("quality").at("signed_volume_mm3")},
        {"mesh_to_reference_relative_error",0.0},{"mapping_basis","user_assignment"},
        {"provenance_authenticated",false},{"source_fidelity_certified",false}};
    return j;
}
int main(){try{
    auto j=v4Job();auto accepted=parse(j);
    check(accepted.singleSurface,"v4 records single-surface contract");
    check(!accepted.analyticReference,"v4 is tessellated, not analytic reference");
    check(accepted.mesh.mesh.triangles.size()==20,"L prism is retained as one closed mesh");
    check(accepted.mesh.report.at("geometry_gate_passed")==true,"concave closed surface passes topology gate");
    check(accepted.document.at("cad_provenance").at("provenance_authenticated")==false,"metadata does not imply authentication");
    auto legacy=meshExperimentDemo();legacy["mesh"]=makeTriangleMeshBundle(lPrism(),"L_Prism","G4_Si",true,{0,0,0},{0,0,0},{{"kind","external_mesh"},{"description","synthetic concave-prism test fixture"}});
    check(rejects(legacy),"legacy v1 convex contract still rejects concavity");

    auto box=meshExperimentDemo();
    auto cubeMesh=inspectTriangleMeshBytes(QByteArray::fromStdString(box["mesh"].dump()),"box.json",true).mesh;
    auto validBox=v4Job(cubeMesh);
    check(!rejects(validBox),"v4 accepts a valid convex single surface too");

    auto bad=j;bad["extra"]=true;check(rejects(bad),"v4 exact top-level fields");
    bad=j;bad["cad_provenance"]["extra"]=true;check(rejects(bad),"provenance exact fields");
    bad=j;bad["schema"]="scdat.mesh-experiment/5";check(rejects(bad),"unknown job schema");
    bad=j;bad["solid"]="analytic_box_reference";check(rejects(bad),"v4 requires tessellated solid");
    bad=j;bad["cad_provenance"]["provenance_authenticated"]=true;check(rejects(bad),"authentication claim rejected");
    bad=j;bad["cad_provenance"]["source_fidelity_certified"]=true;check(rejects(bad),"source-fidelity claim rejected");
    bad=j;bad["cad_provenance"]["embedded_mesh_sha256"]=std::string(64,'0');check(rejects(bad),"embedded mesh hash recomputed");
    bad=j;bad["cad_provenance"]["source_sha256"]="ABC";check(rejects(bad),"hash syntax is lowercase SHA-256");
    bad=j;bad["mesh"]["part"]["points"][0][0]=0.25;check(rejects(bad),"stale embedded hash rejected");
    bad=j;bad["cad_provenance"]["reference_volume_mm3"]=10;check(rejects(bad),"reference volume error rejected");
    bad=j;bad["cad_provenance"]["mesh_to_reference_relative_error"]=0.2;check(rejects(bad),"declared volume error rejected");
    bad=j;bad["cad_provenance"]["part_id"]="other";check(rejects(bad),"provenance part identity required");
    bad=j;bad["cad_provenance"]["material"]="G4_Al";check(rejects(bad),"provenance material identity required");
    bad=j;bad["cad_provenance"]["source_solid_count"]=0;check(rejects(bad),"one source solid required");
    bad=j;bad["cad_provenance"]["mapping_basis"]="guess";check(rejects(bad),"mapping basis enum enforced");
    bad=j;bad["cad_provenance"]["mapping_basis"]="synthetic_software_fixture";check(!rejects(bad),"synthetic software fixture mapping is supported");
    bad=j;bad["cad_provenance"]["original_sensitive"]=false;check(!rejects(bad),"whole-body dosimetry overrides original sensitivity");
    bad=j;bad["source"]["synthetic"]=false;check(rejects(bad),"non-synthetic source rejected");
    bad=j;bad["run"]["source_radius_mm"]=1;check(rejects(bad),"source sphere must enclose the surface");
    bad=j;bad["schema"]="scdat.mesh-experiment/2";check(rejects(bad),"v2 schema cannot carry the single-surface contract");
    bad=j;while(bad["mesh"]["part"]["points"].size()<4097)bad["mesh"]["part"]["points"].push_back({0,0,0});refreshEmbeddedHash(bad);check(rejects(bad),"point budget enforced");
    // Two individually closed, separated boxes must not be smuggled in as a
    // single surface. Their aggregate reference volume is internally consistent.
    TriangleMesh pair=cubeMesh;const auto pointCount=pair.points.size();
    for(auto p:cubeMesh.points)pair.points.push_back({p[0]+50,p[1],p[2]});
    for(auto t:cubeMesh.triangles)pair.triangles.push_back({t[0]+int(pointCount),t[1]+int(pointCount),t[2]+int(pointCount)});
    bad=v4Job(cubeMesh);bad["mesh"]=makeTriangleMeshBundle(pair,"L_Prism","G4_Si",true,{0,0,0},{0,0,0},{{"kind","external_mesh"},{"description","two separated closed boxes"}});
    bad["run"]["source_radius_mm"]=100;refreshEmbeddedHash(bad);bad["cad_provenance"]["reference_volume_mm3"]=2*v4Job(cubeMesh)["cad_provenance"]["reference_volume_mm3"].get<double>();bad["cad_provenance"]["mesh_to_reference_relative_error"]=0.0;
    check(rejects(bad),"disconnected multiple-surface bundle rejected");
    bad=j;bad["mesh"]["part"]["triangles"].erase(0);refreshEmbeddedHash(bad);check(rejects(bad),"open v4 surface rejected");
    bad=j;for(auto& t:bad["mesh"]["part"]["triangles"])std::swap(t[0],t[1]);refreshEmbeddedHash(bad);check(rejects(bad),"inward v4 surface rejected");
    bad=j;bad["mesh"]["part"]["points"].push_back({0,0,0});refreshEmbeddedHash(bad);check(rejects(bad),"unused v4 vertex rejected");
    bad=j;bad["mesh"]["part"]["triangles"][0]={0,0,1};refreshEmbeddedHash(bad);check(rejects(bad),"degenerate v4 surface rejected");
    std::cout<<count<<" single-surface experiment checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<count<<": "<<e.what()<<'\n';return 1;}}
