#include "Instances.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("Instances: "+message);}
void fields(const Json& object,const std::set<std::string>& keys){
    require(object.is_object()&&object.size()==keys.size(),"missing or extra fields");
    for(auto it=object.begin();it!=object.end();++it)require(keys.count(it.key())!=0,"unsupported field: "+it.key());
}
void identifier(const std::string& id){require(!id.empty()&&id!="World"&&id.find('/')==std::string::npos,"ID must be nonempty, not World, and contain no slash: "+id);}
void validateTemplate(const ComponentTemplate& t){
    identifier(t.id);require(!t.volumes.empty(),"template has no material volumes: "+t.id);
    require(t.assemblies.size()+t.volumes.size()<=4096,"template exceeds 4096 nodes");
    Project local;local.assemblies=t.assemblies;local.volumes=t.volumes;
    // This only resolves the local graph, not source clearance or a scoring target.
    // Template geometry enters full material-overlap/source validation when placed.
    resolveGeometry(local);
    for(const auto& a:t.assemblies)identifier(a.id);
    for(const auto& v:t.volumes){identifier(v.id);
        require(v.material.rfind("G4_",0)==0,"invalid template NIST material: "+v.id);
        validateVolumeShape(v);
    }
}
Json assemblyJson(const Assembly& a){return {{"id",a.id},{"parent",a.parent},{"center_mm",a.center},{"rotation_deg",a.rotation},{"visible",a.visible}};}
Json volumeJson(const Volume& v){return {{"id",v.id},{"material",v.material},{"center_mm",v.center},{"size_mm",v.size},
    {"wall_mm",v.wall},{"sensitive",v.sensitive},{"parent",v.parent},{"rotation_deg",v.rotation},{"visible",v.visible}};}
}
Json templateToJson(const ComponentTemplate& t){
    validateTemplate(t);bool analytic=std::any_of(t.volumes.begin(),t.volumes.end(),[](const Volume& v){return v.shape!="box";});
    Json a=Json::array(),v=Json::array();for(const auto& row:t.assemblies)a.push_back(assemblyJson(row));for(const auto& row:t.volumes){auto j=volumeJson(row);if(analytic)j["shape"]=row.shape;v.push_back(j);}
    return {{"schema",analytic?"scdat.component-template/2":"scdat.component-template/1"},{"units",{{"length","mm"},{"angle","deg"}}},{"id",t.id},{"assemblies",a},{"volumes",v}};
}
ComponentTemplate templateFromJson(const Json& j){
    fields(j,{"schema","units","id","assemblies","volumes"});
    bool analytic=j.at("schema")=="scdat.component-template/2";
    require((analytic||j.at("schema")=="scdat.component-template/1")&&j.at("units")==Json({{"length","mm"},{"angle","deg"}}),"unsupported template contract/units");
    require(j.at("assemblies").is_array()&&j.at("volumes").is_array(),"template nodes must be arrays");
    ComponentTemplate t;t.id=j.at("id");
    for(const auto& a:j.at("assemblies")){fields(a,{"id","parent","center_mm","rotation_deg","visible"});
        t.assemblies.push_back({a.at("id"),a.at("parent"),a.at("center_mm").get<std::array<double,3>>(),a.at("rotation_deg").get<std::array<double,3>>(),a.at("visible")});}
    for(const auto& v:j.at("volumes")){auto keys=std::set<std::string>{"id","material","center_mm","size_mm","wall_mm","sensitive","parent","rotation_deg","visible"};if(analytic)keys.insert("shape");fields(v,keys);
        t.volumes.push_back({v.at("id"),v.at("material"),v.at("center_mm").get<std::array<double,3>>(),v.at("size_mm").get<std::array<double,3>>(),
            v.at("wall_mm"),v.at("sensitive"),v.at("parent"),v.at("rotation_deg").get<std::array<double,3>>(),v.at("visible")});if(analytic)t.volumes.back().shape=v.at("shape");}
    validateTemplate(t);return t;
}
Json instanceToJson(const ComponentInstance& i){return {{"id",i.id},{"template_id",i.templateId},{"parent",i.parent},
    {"center_mm",i.center},{"rotation_deg",i.rotation},{"visible",i.visible},{"material_overrides",i.materialOverrides}};}
ComponentInstance instanceFromJson(const Json& j){
    fields(j,{"id","template_id","parent","center_mm","rotation_deg","visible","material_overrides"});
    require(j.at("material_overrides").is_object(),"material overrides must be an object");
    return {j.at("id"),j.at("template_id"),j.at("parent"),j.at("center_mm").get<std::array<double,3>>(),
        j.at("rotation_deg").get<std::array<double,3>>(),j.at("visible"),j.at("material_overrides").get<std::map<std::string,std::string>>()};
}
GeometryModel expandInstances(const Project& p){
    require(p.templates.size()<=128&&p.instances.size()<=512,"too many templates or instances");
    std::map<std::string,const ComponentTemplate*> templates;
    for(const auto& t:p.templates){validateTemplate(t);require(templates.emplace(t.id,&t).second,"duplicate template ID: "+t.id);}
    GeometryModel model;model.volumes=p.volumes;model.assemblies=p.assemblies;
    require(model.volumes.size()+model.assemblies.size()<=4096,"geometry exceeds 4096 nodes");
    for(const auto& i:p.instances){identifier(i.id);auto it=templates.find(i.templateId);require(it!=templates.end(),"unknown template: "+i.templateId);
        const auto& t=*it->second;
        require(model.volumes.size()+model.assemblies.size()+1+t.volumes.size()+t.assemblies.size()<=4096,"expanded geometry exceeds 4096 nodes");
        auto qualify=[&](const std::string& id){return id.empty()?i.id:i.id+"/"+id;};
        auto origin=[&](const std::string& id,const std::string& local,const char* kind){
            require(!model.origins.contains(id),"duplicate generated ID: "+id);
            model.origins[id]={{"instance_id",i.id},{"template_id",t.id},{"local_id",local},{"kind",kind}};};
        std::set<std::string> localVolumes;for(const auto& v:t.volumes)localVolumes.insert(v.id);
        for(const auto& entry:i.materialOverrides)require(localVolumes.count(entry.first)!=0&&(entry.second.empty()||entry.second.rfind("G4_",0)==0),"unknown local part or invalid material override: "+entry.first);
        model.assemblies.push_back({i.id,i.parent,i.center,i.rotation,i.visible});origin(i.id,"","instance_root");
        for(auto a:t.assemblies){auto local=a.id;a.id=qualify(a.id);a.parent=qualify(a.parent);origin(a.id,local,"assembly");model.assemblies.push_back(a);}
        for(auto v:t.volumes){auto local=v.id;v.id=qualify(v.id);v.parent=qualify(v.parent);
            auto material=i.materialOverrides.find(local);if(material!=i.materialOverrides.end())v.material=material->second;
            origin(v.id,local,"volume");model.volumes.push_back(v);}
    }
    return model;
}
ComponentTemplate captureAssembly(const Project& p,const std::string& id,const std::string& templateId){
    identifier(templateId);auto selected=std::find_if(p.assemblies.begin(),p.assemblies.end(),[&](const Assembly& a){return a.id==id;});
    require(selected!=p.assemblies.end(),"capture requires an authored assembly: "+id);
    resolveGeometry(p);std::set<std::string> descendants{id};bool changed=true;
    while(changed){changed=false;for(const auto& a:p.assemblies)if(descendants.count(a.parent)&&descendants.insert(a.id).second)changed=true;}
    for(const auto& i:p.instances)require(!descendants.count(i.parent),"capture does not include nested instances; select authored geometry only");
    ComponentTemplate result;result.id=templateId;
    for(auto a:p.assemblies)if(a.id!=id&&descendants.count(a.id)){if(a.parent==id)a.parent.clear();result.assemblies.push_back(a);}
    for(auto v:p.volumes)if(descendants.count(v.parent)){if(v.parent==id)v.parent.clear();result.volumes.push_back(v);}
    validateTemplate(result);return result;
}
}
