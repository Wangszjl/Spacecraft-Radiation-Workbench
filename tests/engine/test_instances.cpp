#include "Instances.h"
#include "Geometry.h"
#include "RadiationProject.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const std::string& label){++checks;if(!ok)throw std::runtime_error("FAIL: "+label);}
bool near(double a,double b,double eps=1e-9){return std::abs(a-b)<=eps;}
bool near3(const Vector3& a,const Vector3& b,double eps=1e-9){return near(a[0],b[0],eps)&&near(a[1],b[1],eps)&&near(a[2],b[2],eps);}
template<class F> void rejects(F&& f,const std::string& label){bool did=false;try{f();}catch(...){did=true;}check(did,label);}
const Volume& volume(const GeometryModel& model,const std::string& id){
    for(const auto& v:model.volumes)if(v.id==id)return v;
    throw std::runtime_error("missing expanded volume: "+id);
}
const ResolvedVolume& placement(const CompiledGeometry& model,const std::string& id){
    for(const auto& item:model.placements)if(model.volumes[item.index].id==id)return item;
    throw std::runtime_error("missing placement: "+id);
}
}
int main(){
    try{
        Project p=Project::instanceDemo();
        check(p.templates.size()==1&&p.instances.size()==2,"fixture defines one reusable template and two instances");
        auto model=expandInstances(p);
        check(model.assemblies.size()==5&&model.volumes.size()==8,"expansion adds two roots, two local boards and six local volumes");
        check(model.origins.size()==10,"both instances have explicit origin records for root, assembly and three local volumes");
        check(model.origins.at("Unit_A/Chip").at("instance_id")=="Unit_A"&&
              model.origins.at("Unit_A/Chip").at("template_id")=="AvionicsUnit"&&
              model.origins.at("Unit_A/Chip").at("local_id")=="Chip"&&
              model.origins.at("Unit_A/Chip").at("kind")=="volume","origin maps a generated part to stable authored IDs");
        check(model.origins.at("Unit_B/Chip").at("local_id")=="Chip","second instance retains the same template-local ID");
        check(volume(model,"Unit_A/Housing").material=="G4_Al"&&volume(model,"Unit_B/Housing").material=="G4_Cu","material override applies only to its owning instance");
        check(volume(model,"Unit_A/Chip").material=="G4_Si"&&volume(model,"Unit_B/Chip").material=="G4_Si","unoverridden material remains shared template default");

        auto compiled=compileGeometry(p);
        const auto& unitAChip=placement(compiled,"Unit_A/Chip");
        check(near3(unitAChip.transform.translation,{-70.8433790174027,-127.01541589148474,19.313831655073493}),"nested world center matches independent Rz*Ry*Rx composition");
        check(near3(unitAChip.transform.apply({1,0,0}),{-70.20865195129638,-126.2602144026811,19.150148232391686}),"nested world basis point matches independent composed rotation");

        Project hidden=p;hidden.instances[0].visible=false;
        auto hiddenGeometry=compileGeometry(hidden);
        check(compiled.volumes.size()==hiddenGeometry.volumes.size()&&compiled.origins==hiddenGeometry.origins,"hiding an instance preserves expanded physical IDs and origins");
        bool allHidden=true;
        for(const auto& original:compiled.volumes){
            auto it=std::find_if(hiddenGeometry.volumes.begin(),hiddenGeometry.volumes.end(),[&](const Volume& v){return v.id==original.id;});
            check(it!=hiddenGeometry.volumes.end()&&it->material==original.material,"visibility preserves material and volume ID: "+original.id);
            const auto& before=placement(compiled,original.id);const auto& after=placement(hiddenGeometry,original.id);
            check(before.transform.translation==after.transform.translation&&before.transform.rotation==after.transform.rotation,"visibility preserves world transform: "+original.id);
            if(original.id.rfind("Unit_A",0)==0)allHidden=allHidden&&!after.visible;
            else check(before.visible==after.visible,"visibility change does not affect unrelated instance: "+original.id);
        }
        check(allHidden,"hidden root visibility propagates to generated descendants");

        Json templateJson=templateToJson(p.templates[0]);
        auto templateRoundtrip=templateFromJson(templateJson);
        check(templateToJson(templateRoundtrip)==templateJson,"component template JSON round-trip preserves local recipe");
        Json instanceJson=instanceToJson(p.instances[1]);
        check(instanceToJson(instanceFromJson(instanceJson))==instanceJson,"component instance JSON round-trip preserves overrides");

        Json v5=p.toJson();
        check(v5["schema"]=="scdat.radiation/5"&&v5["instance_contract"]=="scdat.component-instances/1","instance recipe serializes as project schema v5");
        check(v5["templates"].size()==1&&v5["instances"].size()==2&&v5["volumes"].size()==2,"saved project stores recipes once and only explicit authored volumes");
        check(v5["volumes"][0]["id"]!="Unit_A/Housing"&&v5["volumes"][1]["id"]!="Unit_B/Housing","expanded volume copies are not persisted");
        Project reopened=Project::fromJson(v5);
        check(reopened.toJson()==v5,"v5 project save/reopen retains authored recipes");
        check(compileGeometry(reopened).origins==compiled.origins,"reopened project expands to the same generated identity map");
        Json downgraded=v5;downgraded["schema"]="scdat.radiation/4";
        rejects([&]{Project::fromJson(downgraded);},"v4 rejects retained v5 template/instance fields");

        auto badTemplate= p;badTemplate.instances[0].templateId="missing-template";
        rejects([&]{badTemplate.validate();},"missing template reference rejected");
        auto badLocalOverride=p;badLocalOverride.instances[0].materialOverrides["UnknownPart"]="G4_Cu";
        rejects([&]{badLocalOverride.validate();},"override for unknown template-local part rejected");
        auto badMaterialOverride=p;badMaterialOverride.instances[0].materialOverrides["Housing"]="Cu";
        rejects([&]{badMaterialOverride.validate();},"override must name NIST material rejected");
        auto duplicate=p;duplicate.instances[1].id="Unit_A";
        rejects([&]{duplicate.validate();},"conflicting instance root ID rejected");
        auto cycle=p;cycle.instances[0].parent="Unit_B";cycle.instances[1].parent="Unit_A";
        rejects([&]{cycle.validate();},"cyclic instance parent hierarchy rejected");
        auto outside=p;outside.instances[0].center={10000,0,0};
        rejects([&]{outside.validate();},"expanded instance outside source sphere rejected");
        auto overlap=p;overlap.instances[1].center=overlap.instances[0].center;overlap.instances[1].rotation=overlap.instances[0].rotation;
        rejects([&]{overlap.validate();},"overlapping expanded instances rejected");
        auto unused=p;ComponentTemplate empty;empty.id="unused-empty";unused.templates.push_back(empty);
        rejects([&]{unused.validate();},"unused template is still structurally validated");

        Project authored=Project::demo();
        authored.assemblies={{"CaptureRoot","",{10,20,30},{0,0,90},true},{"Nested","CaptureRoot",{2,3,4},{5,6,7},true}};
        authored.volumes={{"local_part","G4_Si",{1,2,3},{2,2,1},0,true,"Nested"}};
        auto captured=captureAssembly(authored,"CaptureRoot","CapturedUnit");
        check(captured.id=="CapturedUnit"&&captured.assemblies.size()==1&&captured.volumes.size()==1,"capture includes authored descendants");
        check(captured.assemblies[0].id=="Nested"&&captured.assemblies[0].parent.empty()&&captured.assemblies[0].center==Vector3{2,3,4},"captured nested assembly is expressed in selected local frame");
        check(captured.volumes[0].parent=="Nested"&&captured.volumes[0].center==Vector3{1,2,3},"captured part preserves local placement without baking world transform");
        rejects([&]{captureAssembly(p,"Spacecraft","CaptureWithInstances");},"capture explicitly rejects nested instance descendants");

        // 4094 valid local nodes plus existing authored nodes and a generated root exceed the expanded-node cap.
        auto oversized=p;oversized.templates.clear();oversized.instances.clear();ComponentTemplate large;large.id="large";
        large.assemblies.reserve(4093);for(int i=0;i<4093;++i)large.assemblies.push_back({"A"+std::to_string(i),"",{},{},true});
        large.volumes={{"part","G4_Al",{0,0,0},{1,1,1},0,false}};oversized.templates.push_back(std::move(large));
        oversized.instances={{"LargeInstance","large","",{},{},true,{}}};
        rejects([&]{expandInstances(oversized);},"expanded geometry node limit rejected");

        std::cout<<"instance contract checks: "<<checks<<" passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
