#include "MeshExperimentJob.h"
#include "ConvexSeparation.h"
#include "NonconvexAssembly.h"
#include "StarFanSeparation.h"
#include <QFile>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cmath>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void need(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Mesh experiment: ")+m);}
void fields(const Json& j,std::set<std::string> keys){need(j.is_object()&&j.size()==keys.size(),"missing/extra fields");for(auto i=j.begin();i!=j.end();++i)need(keys.count(i.key())!=0,"unknown field");}
double number(const Json& j,double low,double high){need(j.is_number(),"numeric value required");double v=j.get<double>();need(std::isfinite(v)&&v>=low&&v<=high,"numeric value outside budget");return v;}
double dot(Vector3 a,Vector3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vector3 sub(Vector3 a,Vector3 b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector3 cross(Vector3 a,Vector3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
}
MeshExperimentJob parseMeshExperiment(const QByteArray& bytes,bool allowBlockedInspection){
    need(bytes.size()>0&&bytes.size()<=8*1024*1024,"job must be <=8 MiB");std::vector<std::set<std::string>> keys;
    auto cb=[&](int depth,Json::parse_event_t e,Json& v){need(depth<=32,"nesting budget");if(e==Json::parse_event_t::object_start)keys.emplace_back();else if(e==Json::parse_event_t::key)need(keys.back().insert(v.get<std::string>()).second,"duplicate key");else if(e==Json::parse_event_t::object_end)keys.pop_back();return true;};
    auto j=Json::parse(bytes.toStdString(),cb);bool star=j.at("schema")=="scdat.mesh-experiment/6",nonconvex=star||j.at("schema")=="scdat.mesh-experiment/5",single=j.at("schema")=="scdat.mesh-experiment/4",convex=j.at("schema")=="scdat.mesh-experiment/3",assembly=nonconvex||convex||j.at("schema")=="scdat.mesh-experiment/2";need(nonconvex||bytes.size()<=2*1024*1024,"v1-v4 job must be <=2 MiB");
    if(star){fields(j,{"schema","solid","parts","origins","source","run","separation_contract","covers"});need(j.at("separation_contract")=="scdat.star-fan-separation/1","unsupported star-fan contract");need(j.at("solid")=="tessellated"||j.at("solid")=="orthogonal_l_reference","v6 requires original mesh or exact fixture reference");}
    else if(nonconvex){fields(j,{"schema","solid","parts","origins","source","run","separation_contract"});need(j.at("separation_contract")=="scdat.vertex-hull-separation/1","unsupported vertex-hull contract");need(j.at("solid")=="tessellated"||j.at("solid")=="orthogonal_l_reference","v5 requires tessellated or exact L/box reference");}
    else if(single){fields(j,{"schema","solid","mesh","source","run","cad_provenance"});need(j.at("solid")=="tessellated","single-surface profile requires tessellated geometry");}
    else if(convex){fields(j,{"schema","solid","parts","source","run","separation_contract"});need(j.at("separation_contract")=="scdat.convex-separation/1","unsupported convex separation contract");}
    else if(assembly)fields(j,{"schema","solid","parts","source","run"});else{fields(j,{"schema","solid","mesh","source","run"});need(j.at("schema")=="scdat.mesh-experiment/1","unsupported schema");}
    need(j.at("solid")=="tessellated"||j.at("solid")=="analytic_box_reference"||(nonconvex&&j.at("solid")=="orthogonal_l_reference"),"unsupported solid");
    Json bundles=assembly?j.at("parts"):Json::array({j.at("mesh")});need(bundles.is_array()&&bundles.size()>=(assembly?2u:1u)&&bundles.size()<=(nonconvex?16u:32u),"assembly part count outside profile budget");
    std::vector<TriangleMeshReview> parts;std::set<std::string> ids;size_t points=0,triangles=0,sensitive=0;bool reference=j.at("solid")=="analytic_box_reference"||(nonconvex&&j.at("solid")=="orthogonal_l_reference");std::vector<std::string> profiles;
    for(const auto& bundle:bundles){
    need(bundle.at("schema")=="scdat.radiation-triangle-mesh/1","each part requires one closed surface");
    need(bundle.at("part").at("points").size()<=((single||nonconvex)?4096u:1024u)&&bundle.at("part").at("triangles").size()<=((single||nonconvex)?8192u:2048u),"per-part mesh budget exceeded");points+=bundle.at("part").at("points").size();triangles+=bundle.at("part").at("triangles").size();need(points<=(nonconvex?16384u:4096u)&&triangles<=(nonconvex?32768u:8192u),"total mesh budget exceeded");
    auto review=inspectTriangleMeshBytes(QByteArray::fromStdString(bundle.dump()),"embedded-mesh.json",true);
    need(review.report.at("geometry_gate_passed")==true,"complete closed oriented single-surface self-intersection audit required");
    auto id=review.report.at("id").get<std::string>();need(id!="World"&&ids.insert(id).second,"reserved or duplicate part ID");sensitive+=review.report.at("sensitive").get<bool>()?1:0;
    // Convexity is deliberately restrictive: every vertex must lie behind every outward facet.
    Vector3 lo=review.mesh.points[0],hi=lo;for(auto p:review.mesh.points)for(int k=0;k<3;++k){need(std::abs(p[k])<=10000,"local coordinate budget");lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
    for(int k=0;k<3;++k)need(hi[k]-lo[k]>=.01,"minimum mesh extent is 0.01 mm");
    double span=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});
    for(auto t:review.mesh.triangles){auto a=review.mesh.points[t[0]],n=cross(sub(review.mesh.points[t[1]],a),sub(review.mesh.points[t[2]],a));double norm=std::sqrt(dot(n,n));need(norm>1e-10,"tiny facet");if(!single&&!nonconvex)for(auto p:review.mesh.points)need(dot(n,sub(p,a))/norm<=1e-9*span,"only convex meshes supported by v1-v3");}
    if(nonconvex&&reference)profiles.push_back(nonconvexReferenceProfile(review));
    if(reference&&!nonconvex){double box=(hi[0]-lo[0])*(hi[1]-lo[1])*(hi[2]-lo[2]);need(std::abs(review.report.at("quality").at("signed_volume_mm3").get<double>()-box)<=box*1e-10,"reference mesh must fill its exact local box");}
    parts.push_back(std::move(review));
    }
    need(sensitive>0,"at least one sensitive part required");
    if(single){
        const auto& c=j.at("cad_provenance");fields(c,{"schema","conversion_sha256","source_sha256","inventory_sha256","material_map_sha256","original_mesh_sha256","embedded_mesh_sha256","part_id","material","original_sensitive","scoring_override","source_solid_count","reference_status","reference_volume_mm3","mesh_to_reference_relative_error","mapping_basis","provenance_authenticated","source_fidelity_certified"});
        need(c.at("schema")=="scdat.step-mesh-provenance/1"&&c.at("reference_status")=="numerically_qualified","unsupported STEP provenance profile");
        for(auto key:{"conversion_sha256","source_sha256","inventory_sha256","material_map_sha256","original_mesh_sha256","embedded_mesh_sha256"})need(c.at(key).is_string()&&QRegularExpression("^[a-f0-9]{64}$").match(QString::fromStdString(c.at(key).get<std::string>())).hasMatch(),"invalid provenance digest");
        auto embedded=QByteArray::fromStdString(j.at("mesh").dump());need(c.at("embedded_mesh_sha256")==QCryptographicHash::hash(embedded,QCryptographicHash::Sha256).toHex().toStdString(),"stale embedded mesh digest");
        need(c.at("part_id")==parts[0].report.at("id")&&c.at("material")==parts[0].report.at("material")&&c.at("original_sensitive").is_boolean()&&c.at("scoring_override")=="whole_body_dosimeter","material/ID or explicit scoring provenance mismatch");
        need(c.at("source_solid_count").is_number_integer()&&c.at("source_solid_count")==1,"only a single source solid supported");
        need(c.at("provenance_authenticated").is_boolean()&&c.at("provenance_authenticated")==false&&c.at("source_fidelity_certified").is_boolean()&&c.at("source_fidelity_certified")==false,"unsupported source authentication/fidelity claim");
        need(c.at("mapping_basis")=="user_assignment"||c.at("mapping_basis")=="synthetic_software_fixture","explicit material assignment required");
        double reference=number(c.at("reference_volume_mm3"),1e-12,1e15),error=std::abs(parts[0].report.at("quality").at("signed_volume_mm3").get<double>()/reference-1);
        need(std::abs(number(c.at("mesh_to_reference_relative_error"),0,.05)-error)<=1e-12&&error<=.05,"mesh/reference volume comparison exceeds 5% experimental screen or is inconsistent");
    }
    if(nonconvex)validateNonconvexOrigins(j,parts);
    Json pairs=Json::array();constexpr double separation=1e-4;
    if(!convex&&!nonconvex)for(size_t i=0;i<parts.size();++i)for(size_t k=0;k<i;++k){auto a=parts[k].report.at("world_bounds_mm").get<std::array<double,6>>(),b=parts[i].report.at("world_bounds_mm").get<std::array<double,6>>();int axis=-1;double gap=-1e300;
        for(int d=0;d<3;++d){double g=std::max(b[2*d]-a[2*d+1],a[2*d]-b[2*d+1]);if(g>gap){gap=g;axis=d;}}
        need(gap>separation,"parts require proven world-AABB separation > 0.0001 mm; contact/overlap/unresolved pairs rejected");
        pairs.push_back({{"a",parts[k].report.at("id")},{"b",parts[i].report.at("id")},{"separating_axis",axis},{"gap_mm",gap}});
    }
    const auto& s=j.at("source");fields(s,{"synthetic","particle","energy_min_MeV","energy_max_MeV","omnidirectional_fluence_cm2","angular_distribution"});need(s.at("synthetic")==true&&s.at("synthetic").is_boolean(),"synthetic source required");need(s.at("angular_distribution")=="isotropic","isotropic only");need(s.at("particle")=="e-"||s.at("particle")=="proton","electron or proton only");
    double emin=number(s.at("energy_min_MeV"),.01,1000),emax=number(s.at("energy_max_MeV"),.01,1000);need(emax>emin,"energy interval must increase");double fluence=number(s.at("omnidirectional_fluence_cm2"),1e-10,1e15);
    const auto& r=j.at("run");fields(r,{"histories","seed","source_radius_mm","production_cut_mm"});need(r.at("histories").is_number_integer()&&r.at("seed").is_number_integer(),"histories/seed must be integers");
    auto p=Project::demo();p.name="Synthetic convex-mesh transport experiment";p.environmentSource="SYNTHETIC uniform differential fluence; no orbit or AP8/AE8 evaluation";p.synthetic=true;p.quantity=SpectrumQuantity::Fluence;p.duration=1;p.radius=number(r.at("source_radius_mm"),1,20000);p.cut=number(r.at("production_cut_mm"),.0001,10);p.events=int(number(r.at("histories"),2,1000000));p.seed=long(number(r.at("seed"),1,2147483646));
    Spectrum spectrum;spectrum.particle=s.at("particle");spectrum.energy={emin,emax};spectrum.values={fluence/(emax-emin),fluence/(emax-emin)};spectrum.prepare();p.spectra={spectrum};
    p.volumes.clear();for(const auto& review:parts){auto bounds=review.report.at("world_bounds_mm").get<std::array<double,6>>();Volume v;v.id=review.report.at("id");v.material=review.report.at("material");v.sensitive=review.report.at("sensitive");for(int k=0;k<3;++k){v.center[k]=(bounds[2*k]+bounds[2*k+1])/2;v.size[k]=bounds[2*k+1]-bounds[2*k];}p.volumes.push_back(v);
        for(auto vertex:review.mesh.points){auto w=review.placement.apply(vertex);need(std::sqrt(dot(w,w))<p.radius-.01,"source sphere must enclose every vertex with clearance");}}
    Json separationAudit,sourceEnclosure;
    if(convex||single||nonconvex){
        if(star)separationAudit=auditStarFanSeparation(parts,j.at("covers"));else if(nonconvex)separationAudit=auditVertexHullSeparation(parts);else if(convex)separationAudit=auditConvexSeparation(parts);else separationAudit={{"schema","scdat.single-surface-pairs/1"},{"pair_count",0},{"pairs",Json::array()},{"separation_gate_passed",true},{"complete",true},{"transport_allowed",false}};
        sourceEnclosure=auditConvexSourceEnclosure(parts,p.radius);if(single||nonconvex)sourceEnclosure["method"]="Outward interval affine vertices; enclosing sphere contains every triangle and its bounded interior, including concave surfaces";need(sourceEnclosure.at("passed")==true,"actual mesh vertices exceed the 99% source enclosure");
        // The reused Project validates source/material fields and is never passed to
        // makeGeometry(). Independent actual-vertex enclosure and pair witnesses
        // above govern geometry; overlapping AABBs cannot serve as physical boxes.
        for(size_t i=0;i<p.volumes.size();++i){p.volumes[i].center={.03*(double(i)-double(p.volumes.size()-1)/2),0,0};p.volumes[i].size={.01,.01,.01};}
        if(!allowBlockedInspection&&separationAudit.at("separation_gate_passed")!=true){std::string why=star?"strict star-fan cover/separation blocked: ":nonconvex?"strict vertex-hull separation blocked: ":"strict convex separation blocked: ";for(const auto& pair:separationAudit.at("pairs"))if(pair.at("status")!="separated"){why+=pair.at("a").get<std::string>()+" / "+pair.at("b").get<std::string>()+" ("+pair.at("status").get<std::string>()+")";break;}throw std::runtime_error(why);}
    }else separationAudit={{"schema","scdat.experimental-aabb-separation/1"},{"method","strict positive world-AABB axis gap; conservative sufficient separation only"},{"minimum_gap_mm",separation},{"pair_count",pairs.size()},{"complete",true},{"pairs",pairs}};
    p.validate();MeshExperimentJob out;out.source=std::move(p);out.mesh=parts.front();out.document=j;out.bytes=bytes;out.analyticReference=reference;out.parts=std::move(parts);out.assembly=assembly;out.convexSeparation=convex;out.singleSurface=single;out.starFanAssembly=star;out.nonconvexAssembly=nonconvex;out.referenceProfiles=std::move(profiles);out.sourceEnclosure=sourceEnclosure;out.separationAudit=std::move(separationAudit);return out;
}
MeshExperimentJob loadMeshExperiment(const QString& path,bool allowBlockedInspection){QFile f(path);need(f.open(QIODevice::ReadOnly)&&f.size()<=8*1024*1024,"cannot read bounded job");return parseMeshExperiment(f.readAll(),allowBlockedInspection);}
Json meshExperimentDemo(const std::string& particle){
    TriangleMesh m{{{-10,-10,-1},{10,-10,-1},{10,10,-1},{-10,10,-1},{-10,-10,1},{10,-10,1},{10,10,1},{-10,10,1}},{{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}}};
    return {{"schema","scdat.mesh-experiment/1"},{"solid","tessellated"},{"mesh",makeTriangleMeshBundle(m,"MeshSi","G4_Si",true,{0,0,0},{0,0,0},{{"kind","external_mesh"},{"description","SYNTHETIC 20 x 20 x 2 mm silicon box; software transport regression"}})},
        {"source",{{"synthetic",true},{"particle",particle},{"energy_min_MeV",particle=="proton"?10:1},{"energy_max_MeV",particle=="proton"?20:2},{"omnidirectional_fluence_cm2",1e7},{"angular_distribution","isotropic"}}},
        {"run",{{"histories",10000},{"seed",19780503},{"source_radius_mm",30},{"production_cut_mm",.01}}}};
}
Json meshAssemblyExperimentDemo(const std::string& particle,bool shielded){
    auto job=meshExperimentDemo(particle);job["schema"]="scdat.mesh-experiment/2";auto cube=job.at("mesh");job.erase("mesh");job["parts"]=Json::array();
    auto box=[&](const char* id,const char* material,bool sensitive,Vector3 center,Vector3 size){auto b=cube;b["part"]["id"]=id;b["part"]["material"]=material;b["part"]["sensitive"]=sensitive;b["part"]["center"]=center;
        for(size_t i=0;i<b["part"]["points"].size();++i)for(int d=0;d<3;++d)b["part"]["points"][i][d]=cube["part"]["points"][i][d].get<double>()*size[d]/(d==2?2.:20.);
        b["declared_source"]["description"]="SYNTHETIC separated panel/target fixture; not a sealed shell or spacecraft CAD";job["parts"].push_back(b);};
    if(shielded){box("Shield_Xminus","G4_Al",false,{-16,0,0},{2,29.6,29.6});box("Shield_Xplus","G4_Al",false,{16,0,0},{2,29.6,29.6});box("Shield_Yminus","G4_Al",false,{0,-16,0},{29.6,2,29.6});box("Shield_Yplus","G4_Al",false,{0,16,0},{29.6,2,29.6});box("Shield_Zminus","G4_Al",false,{0,0,-16},{29.6,29.6,2});box("Shield_Zplus","G4_Al",false,{0,0,16},{29.6,29.6,2});}
    box("Device_A","G4_Si",true,{-5,0,0},{8,12,1});box("Device_B","G4_Si",true,{5,0,0},{8,12,1});job["run"]["source_radius_mm"]=35;job["run"]["histories"]=60000;return job;
}
Json meshRotatedExperimentDemo(const std::string& particle){
    auto job=meshAssemblyExperimentDemo(particle,false);job["schema"]="scdat.mesh-experiment/3";job["separation_contract"]="scdat.convex-separation/1";
    job["parts"][0]["part"]["center"]={-3.6,-3.6,0};job["parts"][1]["part"]["center"]={3.6,3.6,0};for(auto& p:job["parts"])p["part"]["rotation_deg"]={0,0,45};
    auto panel=meshAssemblyExperimentDemo(particle,true).at("parts").at(4);panel["part"]["id"]="Rotated_Al_panel";panel["part"]["center"]={0,0,-3};panel["part"]["rotation_deg"]={0,0,45};job["parts"].push_back(panel);
    for(auto& p:job["parts"])p["declared_source"]["description"]="SYNTHETIC rotated convex assembly; overlapping sensitive-body AABBs; not spacecraft CAD";return job;
}
}
