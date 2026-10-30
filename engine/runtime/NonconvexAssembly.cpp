#include "NonconvexAssembly.h"
#include "ConvexSeparation.h"
#include <QCryptographicHash>
#include <cmath>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("Nonconvex assembly: ")+why);}
void fields(const Json& j,std::set<std::string> names){need(j.is_object()&&j.size()==names.size(),"missing/extra origin fields");for(auto i=j.begin();i!=j.end();++i)need(names.count(i.key())!=0,"unknown origin field");}
std::string hash(const Json& j){return QCryptographicHash::hash(QByteArray::fromStdString(j.dump()),QCryptographicHash::Sha256).toHex().toStdString();}
TriangleMesh lMesh(){
    TriangleMesh m;const std::array<std::array<double,2>,6> polygon={{{-10,-10},{10,-10},{10,0},{0,0},{0,10},{-10,10}}};
    for(double z:{-2.,2.})for(auto p:polygon)m.points.push_back({p[0],p[1],z});
    const std::array<std::array<size_t,3>,4> cap={{{0,1,3},{1,2,3},{0,3,5},{3,4,5}}};
    for(auto t:cap)m.triangles.push_back({t[0],t[2],t[1]});for(auto t:cap)m.triangles.push_back({t[0]+6,t[1]+6,t[2]+6});
    for(size_t i=0;i<6;++i){size_t k=(i+1)%6;m.triangles.push_back({i,k,k+6});m.triangles.push_back({i,k+6,i+6});}return m;
}
Json syntheticOrigin(const Json& bundle,const char* description){return {{"kind","synthetic_fixture"},{"part_id",bundle.at("part").at("id")},{"description",description},{"mesh_sha256",hash(bundle)}};}
Json box(const char* id,Vector3 center){auto bundle=meshExperimentDemo().at("mesh");bundle["part"]["id"]=id;bundle["part"]["center"]=center;for(auto& p:bundle["part"]["points"]){p[0]=p[0].get<double>()*.2;p[1]=p[1].get<double>()*.3;p[2]=p[2].get<double>()*.5;}bundle["declared_source"]["description"]="SYNTHETIC 4 x 6 x 1 mm silicon sensitive box; not a real device qualification";return bundle;}
}
Json auditVertexHullSeparation(const std::vector<TriangleMeshReview>& parts){
    auto audit=auditConvexSeparation(parts);audit["schema"]="scdat.vertex-hull-separation/1";audit["method"]="Outward interval projections enclose every affine vertex and its hull, hence every bounded triangular solid including concave bodies; strict hull gap is sufficient, never necessary";audit["body_convexity_required"]=false;audit["convex_hulls_must_be_separated"]=true;return audit;
}
std::string nonconvexReferenceProfile(const TriangleMeshReview& review){
    auto l=lMesh();if(review.mesh.points==l.points&&review.mesh.triangles==l.triangles)return "orthogonal_l";
    const auto& m=review.mesh;need(m.points.size()==8&&m.triangles.size()==12,"reference only recognizes exact L fixture or box corners");Vector3 lo=m.points[0],hi=lo;for(auto p:m.points)for(int d=0;d<3;++d){lo[d]=std::min(lo[d],p[d]);hi[d]=std::max(hi[d],p[d]);}std::set<Vector3> corners;for(auto p:m.points){for(int d=0;d<3;++d)need(p[d]==lo[d]||p[d]==hi[d],"reference point is not a box corner");corners.insert(p);}need(corners.size()==8,"reference has duplicate box corners");for(auto t:m.triangles){bool face=false;for(int d=0;d<3;++d)face|=m.points[t[0]][d]==m.points[t[1]][d]&&m.points[t[1]][d]==m.points[t[2]][d];need(face,"reference triangle is not a box face");}return "box";
}
void validateNonconvexOrigins(const Json& job,const std::vector<TriangleMeshReview>& parts){
    const auto& origins=job.at("origins");need(origins.is_array()&&origins.size()==parts.size(),"one origin per part required");
    for(size_t i=0;i<parts.size();++i){const auto& o=origins[i];const auto& bundle=job.at("parts")[i];need(o.at("part_id")==parts[i].report.at("id")&&o.at("mesh_sha256")==hash(bundle),"origin ID/embedded mesh digest mismatch");
        if(o.at("kind")=="synthetic_fixture"){fields(o,{"kind","part_id","description","mesh_sha256"});need(o.at("description").is_string()&&!o.at("description").get<std::string>().empty()&&o.at("description").get<std::string>().size()<=4096,"explicit synthetic description required");}
        else {fields(o,{"kind","part_id","base_job","mesh_sha256","placement_basis","sensitivity_override"});need(o.at("kind")=="step_instance"&&o.at("placement_basis")=="user_defined_instance","unsupported CAD instance origin");need(o.at("base_job").at("schema")=="scdat.mesh-experiment/4","CAD instance requires an explicit v4 base, no recursive assembly");auto base=parseMeshExperiment(QByteArray::fromStdString(o.at("base_job").dump()));auto expected=base.document.at("mesh");expected["part"]["id"]=bundle.at("part").at("id");expected["part"]["center"]=bundle.at("part").at("center");expected["part"]["rotation_deg"]=bundle.at("part").at("rotation_deg");need(o.at("sensitivity_override")=="preserve"||o.at("sensitivity_override")=="whole_body_dosimeter","explicit sensitivity override required");expected["part"]["sensitive"]=o.at("sensitivity_override")=="whole_body_dosimeter"?Json(true):base.document.at("cad_provenance").at("original_sensitive");need(expected==bundle,"instance changed source vertices/facets/material/units/description beyond explicit placement/ID/sensitivity");}
    }
}
Json nonconvexAssemblyDemo(const std::string& particle){
    auto job=meshExperimentDemo(particle);job.erase("mesh");job["schema"]="scdat.mesh-experiment/5";job["solid"]="tessellated";job["separation_contract"]="scdat.vertex-hull-separation/1";job["parts"]=Json::array();job["origins"]=Json::array();
    for(int i=0;i<2;++i){auto bundle=makeTriangleMeshBundle(lMesh(),i?"Concave_Al_B":"Concave_Al_A","G4_Al",false,i?Vector3{10,10,0}:Vector3{-10,-10,0},{0,0,45},{{"kind","external_mesh"},{"description","SYNTHETIC L-shaped 20 x 20 x 4 mm prism; polygon area 300 mm2; not spacecraft CAD"}});job["parts"].push_back(bundle);job["origins"].push_back(syntheticOrigin(bundle,"SYNTHETIC L-shaped shielding body with user-defined rotated placement"));}
    for(int i=0;i<2;++i){auto bundle=box(i?"Device_B":"Device_A",i?Vector3{3,0,-10}:Vector3{-3,0,-10});job["parts"].push_back(bundle);job["origins"].push_back(syntheticOrigin(bundle,"SYNTHETIC silicon dosimeter, no calibrated device failure model"));}
    job["run"]["source_radius_mm"]=50;job["run"]["histories"]=60000;return job;
}
Json prepareStepMeshAssembly(const QString& manifest,const std::string& particle){
    auto prepared=prepareStepMeshExperiment(manifest,particle);const auto& base=prepared.at("job");auto review=parseMeshExperiment(QByteArray::fromStdString(base.dump()));Vector3 lo=review.mesh.mesh.points[0],hi=lo;for(auto p:review.mesh.mesh.points)for(int d=0;d<3;++d){lo[d]=std::min(lo[d],p[d]);hi[d]=std::max(hi[d],p[d]);}Vector3 mid{};double r2=0;for(int d=0;d<3;++d){mid[d]=(lo[d]+hi[d])/2;r2+=(hi[d]-lo[d])*(hi[d]-lo[d])/4;}double radius=std::sqrt(r2);auto job=nonconvexAssemblyDemo(particle);job["parts"]=Json::array();job["origins"]=Json::array();
    for(int i=0;i<2;++i){auto bundle=base.at("mesh");bundle["part"]["id"]=i?"CAD_Instance_B":"CAD_Instance_A";Vector3 angles{0,0,i?-27.:27.};Project probe;Volume volume;volume.id="Probe";volume.rotation=angles;probe.volumes={volume};auto rotation=resolveGeometry(probe).at(0).transform;auto rotatedMid=rotation.apply(mid);Vector3 translation{-rotatedMid[0]+(i?1.:-1.)*(radius+20),-rotatedMid[1],-rotatedMid[2]};bundle["part"]["center"]=translation;bundle["part"]["rotation_deg"]=angles;bundle["part"]["sensitive"]=base.at("cad_provenance").at("original_sensitive");job["parts"].push_back(bundle);job["origins"].push_back({{"kind","step_instance"},{"part_id",bundle.at("part").at("id")},{"base_job",base},{"mesh_sha256",hash(bundle)},{"placement_basis","user_defined_instance"},{"sensitivity_override","preserve"}});}
    for(int i=0;i<2;++i){auto bundle=box(i?"Device_B":"Device_A",i?Vector3{0,6,0}:Vector3{0,-6,0});job["parts"].push_back(bundle);job["origins"].push_back(syntheticOrigin(bundle,"SYNTHETIC silicon dosimeter between two user-defined CAD instances"));}
    job["run"]["source_radius_mm"]=(2*radius+35)/.95;job["run"]["histories"]=80000;parseMeshExperiment(QByteArray::fromStdString(job.dump()));need(prepareStepMeshExperiment(manifest,particle).at("step_conversion_review")==prepared.at("step_conversion_review"),"CAD package changed during assembly preparation");
    return {{"schema","scdat.step-mesh-assembly-preparation/1"},{"job",job},{"step_conversion_review",prepared.at("step_conversion_review")},{"original_assembly_restored",false},{"runtime_geometry_executed",false},{"transport_allowed",false},{"scope","Two user-defined instances of one qualified STEP mesh plus synthetic silicon dosimeters. Original local vertices/material retained; placement and IDs are explicit experimental choices, not recovered source assembly."}};
}
}
