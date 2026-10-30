#include "ViewProperties.h"
#include "ProjectFile.h"
#include "../Materials.h"
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("View properties: "+message);}
bool near(double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-12+1e-9*std::max(std::abs(a),std::abs(b));}
bool relative(double a,double b){return std::isfinite(a)&&std::isfinite(b)&&a>0&&b>0&&std::abs(a-b)<=1e-9*std::max(a,b);}
bool sha(const Json& value){if(!value.is_string())return false;auto s=value.get<std::string>();return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
Json sorted(Json snapshot){for(auto& m:snapshot["materials"]){auto& e=m["elements"];std::sort(e.begin(),e.end(),[](const Json& a,const Json& b){return a.at("Z")<b.at("Z");});}auto& m=snapshot["materials"];std::sort(m.begin(),m.end(),[](const Json& a,const Json& b){return a.at("id")<b.at("id");});return snapshot;}
double positive(const Json& j,const char* key){double x=j.at(key).get<double>();require(std::isfinite(x)&&x>0,std::string("invalid ")+key);return x;}
bool descendant(const CompiledGeometry& g,const Volume& v,const std::string& id){auto parent=v.parent;for(size_t depth=0;!parent.empty()&&depth<=g.assemblies.size();++depth){if(parent==id)return true;auto a=std::find_if(g.assemblies.begin(),g.assemblies.end(),[&](const Assembly& a){return a.id==parent;});if(a==g.assemblies.end())break;parent=a->parent;}return false;}
}
ViewPropertiesContext makeViewPropertiesContext(const Project& p,bool diagnostic){
    if(!diagnostic)p.validate();ViewPropertiesContext c;c.project=p;c.geometry=compileGeometry(p);c.diagnostic=diagnostic;if(!diagnostic)c.materialSnapshot=p.materialSnapshot;return c;
}
void bindViewPropertiesResult(ViewPropertiesContext& c,const QByteArray& bytes,const Json& identity,const QString& path){
    require(!c.diagnostic,"diagnostic preview cannot bind transport evidence");require(!bytes.isEmpty()&&bytes.size()<=32*1024*1024,"result size must be 1..32 MiB");
    require(identity.at("file_name").is_string()&&identity.at("size_bytes").get<qint64>()>0&&sha(identity.at("sha256")),"invalid input identity");auto result=Json::parse(bytes.toStdString());
    require(result.at("status")=="completed"&&result.at("project_sha256")==identity.at("sha256"),"result input SHA differs");
    auto actual=Project::fromJson(result.at("project")).toJson(),expected=c.project.toJson();actual.erase("provenance");expected.erase("provenance");require(actual==expected,"result physical input differs from preview");
    const auto& materials=result.at("material_audit");auto snapshot=materials.at("snapshot");auto validated=c.project;validated.materialSnapshot=snapshot;validateMaterialSnapshot(validated);
    require(snapshot.at("engine_version")==result.at("engine_version"),"material engine differs");require(materials.at("snapshot_sha256")==QCryptographicHash::hash(QByteArray::fromStdString(snapshot.dump()),QCryptographicHash::Sha256).toHex().toStdString(),"material snapshot SHA differs");
    require(materials.at("status")== (c.project.materialSnapshot.is_null()?"resolved_at_run_unpinned":"pinned_match"),"material pin status differs");if(!c.project.materialSnapshot.is_null())require(sorted(snapshot)==sorted(c.project.materialSnapshot),"pinned material definitions differ");
    std::map<std::string,double> densities;for(const auto& m:snapshot.at("materials"))densities.emplace(m.at("id"),m.at("density_kg_m3"));
    auto model=geometrySnapshot(c.project);require(result.at("geometry_audit").at("contract")==model.at("contract"),"geometry audit contract differs");std::map<std::string,Json> rows;for(const auto& row:result.at("geometry_audit").at("volumes"))require(rows.emplace(row.at("id"),row).second,"duplicate geometry audit ID");require(rows.size()==c.geometry.volumes.size(),"geometry audit coverage differs");
    std::set<std::string> sensitive;
    for(const auto& v:model.at("volumes")){auto id=v.at("id").get<std::string>();require(rows.count(id)!=0,"missing geometry audit ID");const auto& audit=rows.at(id);require(audit.at("material")==v.at("material")&&audit.at("sensitive")==v.at("sensitive"),"audit material/sensitive binding differs");
        require(audit.value("shape",std::string("box"))==v.value("shape",std::string("box")),"audit shape differs");for(const char* key:{"size_mm","wall_mm","parent","local_center_mm","local_rotation_deg"})require(audit.at(key)==v.at(key),std::string("audit geometry differs: ")+key);
        auto name=v.at("material").get<std::string>();require(densities.count(name)&&relative(positive(audit,"density_kg_m3"),densities.at(name)),"audit density differs");
        auto volume=positive(audit,"geant4_material_volume_mm3");require(relative(volume,v.at("material_volume_mm3"))&&relative(positive(audit,"material_volume_mm3"),v.at("material_volume_mm3")),"audit volume differs");require(relative(positive(audit,"mass_kg"),volume*densities.at(name)*1e-9),"audit mass differs from density and volume");
        for(int i=0;i<3;++i){require(near(audit.at("geant4_world_center_mm")[i],v.at("world_center_mm")[i]),"audit world center differs");for(int j=0;j<3;++j)require(near(audit.at("geant4_world_rotation")[i][j],v.at("world_rotation")[i][j]),"audit rotation differs");}
        if(v.at("sensitive")==true)sensitive.insert(id);
    }
    std::set<std::string> targets;for(const auto& target:result.at("targets")){auto id=target.at("id").get<std::string>();require(sensitive.count(id)&&targets.insert(id).second,"unexpected or duplicate target");require(relative(positive(target,"mass_kg"),rows.at(id).at("mass_kg")),"target mass differs from geometry audit");}
    require(targets==sensitive,"target coverage differs");require(sha(result.at("worker").at("sha256")),"invalid Worker fingerprint");
    // Publish only after all checks, so failed binding never partly replaces evidence.
    c.materialSnapshot=std::move(snapshot);c.geometryAudit=result.at("geometry_audit");c.targets=result.at("targets");c.inputIdentity=identity;c.resultIdentity=sourceFileIdentity(path,bytes);c.worker=result.at("worker");c.engineVersion=result.at("engine_version");c.resultStatus="completed";
}
Json viewSelectionProperties(const ViewPropertiesContext& c,const std::string& id){
    Json out={{"schema","scdat.radiation-view-properties/1"},{"id",id},{"diagnostic_only",c.diagnostic},{"kind","volume"},{"parent",nullptr},{"local_center_mm",nullptr},{"local_rotation_deg",nullptr},{"world_center_mm",nullptr},{"world_rotation",nullptr},{"world_bounds_mm",nullptr},{"shape",nullptr},{"size_mm",nullptr},{"wall_mm",nullptr},{"material",nullptr},{"sensitive",nullptr},{"visible",nullptr},{"instance_origin",nullptr},{"analytic_volume_mm3",nullptr},{"density_kg_m3",nullptr},{"estimated_mass_kg",nullptr},{"transport_mass_kg",nullptr},{"material_definition",nullptr},{"material_source",c.diagnostic?"diagnostic_unverified":"unavailable"},{"result",nullptr},{"descendant_volume_ids",Json::array()},
        {"source",{{"input",c.inputIdentity},{"result",c.resultIdentity},{"result_status",c.resultStatus.empty()?Json(nullptr):Json(c.resultStatus)},{"worker",c.worker},{"engine_version",c.engineVersion.empty()?Json(nullptr):Json(c.engineVersion)},{"material_provider",c.materialSnapshot.is_null()?Json(nullptr):c.materialSnapshot.at("provider")},{"material_engine_version",c.materialSnapshot.is_null()?Json(nullptr):c.materialSnapshot.at("engine_version")}}}};
    auto v=std::find_if(c.geometry.volumes.begin(),c.geometry.volumes.end(),[&](const Volume& v){return v.id==id;});
    if(v!=c.geometry.volumes.end()){
        const auto index=size_t(v-c.geometry.volumes.begin());const auto& placement=c.geometry.placements.at(index);out["parent"]=v->parent;out["local_center_mm"]=v->center;out["local_rotation_deg"]=v->rotation;out["world_center_mm"]=placement.transform.translation;out["world_rotation"]=placement.transform.rotation;out["shape"]=v->shape;out["size_mm"]=v->size;out["wall_mm"]=v->wall;out["material"]=v->material;out["sensitive"]=v->sensitive;out["visible"]=placement.visible;out["local_visible"]=v->visible;
        try{validateVolumeShape(*v);out["analytic_volume_mm3"]=materialVolume(*v);auto b=worldBounds(*v,placement.transform);if(std::all_of(b.begin(),b.end(),[](double x){return std::isfinite(x);}))out["world_bounds_mm"]=b;}catch(const std::exception&){}
        if(!c.diagnostic&&!c.materialSnapshot.is_null())for(const auto& m:c.materialSnapshot.at("materials"))if(m.at("id")==v->material){out["material_definition"]=m;out["density_kg_m3"]=m.at("density_kg_m3");out["material_source"]=c.geometryAudit.is_null()?"project_material_snapshot":"saved_transport_audit";if(!out["analytic_volume_mm3"].is_null())out["estimated_mass_kg"]=out["analytic_volume_mm3"].get<double>()*m.at("density_kg_m3").get<double>()*1e-9;}
        if(!c.geometryAudit.is_null())for(const auto& row:c.geometryAudit.at("volumes"))if(row.at("id")==id){out["transport_mass_kg"]=row.at("mass_kg");break;}
        if(!c.targets.is_null())for(const auto& t:c.targets)if(t.at("id")==id){out["result"]=t;break;}
    }else{
        auto assembly=std::find_if(c.geometry.assemblies.begin(),c.geometry.assemblies.end(),[&](const Assembly& a){return a.id==id;});require(assembly!=c.geometry.assemblies.end(),"unknown ID: "+id);
        out["kind"]="assembly";out["parent"]=assembly->parent;out["local_center_mm"]=assembly->center;out["local_rotation_deg"]=assembly->rotation;out["local_visible"]=assembly->visible;
        Project resolver;resolver.assemblies=c.geometry.assemblies;Volume probe;probe.id="__view_properties_probe__";while(std::any_of(resolver.assemblies.begin(),resolver.assemblies.end(),[&](const Assembly& a){return a.id==probe.id;}))probe.id+="_";probe.parent=id;resolver.volumes={probe};const auto resolved=compileGeometry(resolver).placements.at(0);out["world_center_mm"]=resolved.transform.translation;out["world_rotation"]=resolved.transform.rotation;out["visible"]=resolved.visible;
        for(const auto& child:c.geometry.volumes)if(descendant(c.geometry,child,id))out["descendant_volume_ids"].push_back(child.id);
    }
    if(c.geometry.origins.contains(id))out["instance_origin"]=c.geometry.origins.at(id);return out;
}
}
