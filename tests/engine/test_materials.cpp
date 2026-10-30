#include "Materials.h"
#include "RadiationProject.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const std::string& label){++checks;if(!ok)throw std::runtime_error("FAIL: "+label);}
template<class F> void rejects(F&& f,const std::string& label){bool did=false;try{f();}catch(...){did=true;}check(did,label);}
Json syntheticSnapshot(){
    return {{"schema","scdat.nist-materials/1"},{"engine_version","11.4.3"},
        {"provider","Geant4 G4NistManager"},{"isotopes_requested",false},
        {"materials",Json::array({{{"id","G4_Si"},{"density_kg_m3",2330.0},{"state","solid"},
            {"temperature_K",293.15},{"pressure_Pa",101325.0},
            {"elements",Json::array({{{"name","Si"},{"symbol","Si"},{"Z",14},
                {"A_g_mol",28.0855},{"mass_fraction",1.0}}})}}})}};
}
Project siliconProject(){
    Project p=Project::demo();
    Volume target{"Si_target","G4_Si",{0,0,0},{10,10,1},0,true};
    p.volumes={target};
    p.materialSnapshot=syntheticSnapshot();
    return p;
}
}
int main(){
    try{
        auto p=siliconProject();
        validateMaterialSnapshot(p);
        Json v4=p.toJson();
        check(v4["schema"]=="scdat.radiation/4","material snapshot selects schema v4");
        check(v4["geometry_contract"]=="scdat.box-assembly/1"&&v4.contains("assemblies"),"v4 always includes geometry contract and assemblies");
        check(v4["material_contract"]=="scdat.nist-materials/1"&&v4["material_snapshot"]==syntheticSnapshot(),"v4 serializes strict material contract and snapshot");
        Project round=Project::fromJson(v4);
        check(round.materialSnapshot==p.materialSnapshot,"v4 material snapshot round-trips");
        check(round.toJson()["schema"]=="scdat.radiation/4","round-trip remains schema v4");
        auto origin4=p;origin4.provenance["origin_schema"]="scdat.radiation/4";
        try{origin4.validate();check(true,"provenance origin schema 4 accepted");}catch(...){check(false,"provenance origin schema 4 accepted");}

        Json mislabeled3=v4;mislabeled3["schema"]="scdat.radiation/3";
        rejects([&]{Project::fromJson(mislabeled3);},"v3 refuses material fields");
        Json missing=v4;missing.erase("material_snapshot");
        rejects([&]{Project::fromJson(missing);},"v4 requires material snapshot");
        Json nullSnapshot=v4;nullSnapshot["material_snapshot"]=nullptr;
        rejects([&]{Project::fromJson(nullSnapshot);},"v4 rejects null material snapshot");
        Json wrongContract=v4;wrongContract["material_contract"]="scdat.nist-materials/999";
        rejects([&]{Project::fromJson(wrongContract);},"unknown material contract rejected");

        auto rejectSnapshot=[&](const std::string& name,const auto& mutate){
            auto bad=p;mutate(bad.materialSnapshot);rejects([&]{validateMaterialSnapshot(bad);},name);
        };
        rejectSnapshot("material not assigned to any volume rejected",[](Json& j){j["materials"][0]["id"]="G4_Al";});
        rejectSnapshot("unknown snapshot field rejected",[](Json& j){j["unexpected"]=1;});
        rejectSnapshot("unknown material field rejected",[](Json& j){j["materials"][0]["unexpected"]=1;});
        rejectSnapshot("unknown element field rejected",[](Json& j){j["materials"][0]["elements"][0]["unexpected"]=1;});
        rejectSnapshot("empty material list rejected",[](Json& j){j["materials"]=Json::array();});
        rejectSnapshot("duplicate material ID rejected",[](Json& j){j["materials"].push_back(j["materials"][0]);});
        rejectSnapshot("duplicate element Z rejected",[](Json& j){j["materials"][0]["elements"].push_back(j["materials"][0]["elements"][0]);});
        rejectSnapshot("non-finite density rejected",[](Json& j){j["materials"][0]["density_kg_m3"]=std::numeric_limits<double>::infinity();});
        rejectSnapshot("non-positive temperature rejected",[](Json& j){j["materials"][0]["temperature_K"]=0;});
        rejectSnapshot("negative pressure rejected",[](Json& j){j["materials"][0]["pressure_Pa"]=-1;});
        rejectSnapshot("invalid element Z rejected",[](Json& j){j["materials"][0]["elements"][0]["Z"]=0;});
        rejectSnapshot("non-positive atomic weight rejected",[](Json& j){j["materials"][0]["elements"][0]["A_g_mol"]=-1;});
        rejectSnapshot("zero mass fraction rejected",[](Json& j){j["materials"][0]["elements"][0]["mass_fraction"]=0;});
        rejectSnapshot("mass fractions must sum to one",[](Json& j){j["materials"][0]["elements"][0]["mass_fraction"]=0.9;});
        rejectSnapshot("provider fixed to Geant4 NIST",[](Json& j){j["provider"]="other";});
        rejectSnapshot("isotopes request must be false",[](Json& j){j["isotopes_requested"]=true;});
        rejectSnapshot("engine version must be semantic numeric triple",[](Json& j){j["engine_version"]="latest";});
        rejectSnapshot("snapshot schema fixed",[](Json& j){j["schema"]="scdat.nist-materials/2";});

        // Earlier schemas preserve their existing schema when no snapshot is attached.
        auto legacy=Project::demo();
        check(legacy.toJson()["schema"]=="scdat.radiation/2","v2 remains v2 without material snapshot");
        auto v3p=Project::assemblyDemo();
        check(v3p.toJson()["schema"]=="scdat.radiation/3","v3 remains v3 without material snapshot");
        auto snapshotOnV2=legacy.toJson();snapshotOnV2["material_snapshot"]=syntheticSnapshot();
        rejects([&]{Project::fromJson(snapshotOnV2);},"v2 rejects material snapshot field");
        auto contractOnV3=v3p.toJson();contractOnV3["material_contract"]="scdat.nist-materials/1";
        rejects([&]{Project::fromJson(contractOnV3);},"v3 rejects material contract field");

        auto unassigned=Project::demo();
        unassigned.volumes[0].material="";unassigned.volumes[1].material="";
        const auto missingIds=unassignedMaterialIds(unassigned);
        check(missingIds.size()==2,"unassigned material finder returns two IDs");
        check(missingIds[0]==unassigned.volumes[0].id&&missingIds[1]==unassigned.volumes[1].id,"unassigned material finder identifies exact volume IDs without full validation");
        std::cout<<"material contract checks: "<<checks<<" passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
