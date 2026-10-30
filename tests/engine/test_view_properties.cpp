#include "runtime/ViewProperties.h"
#include "runtime/ProjectFile.h"
#include <QCryptographicHash>
#include <iostream>
#include <stdexcept>
using namespace spacerad::engine;
int count=0;
void check(bool ok,const char* m){++count;if(!ok)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,m);}
Json material(std::string id,double density,int z,std::string symbol){return {{"id",id},{"density_kg_m3",density},{"state","solid"},{"temperature_K",293.15},{"pressure_Pa",101325.0},{"elements",Json::array({{{"name",symbol},{"symbol",symbol},{"Z",z},{"A_g_mol",double(z*2)},{"mass_fraction",1.0}}})}};}
int main(){try{
    auto p=Project::demo();p.radius=100;p.volumes={Volume{"A","G4_Si",{0,0,0},{10,10,10},0,true},Volume{"B","G4_Al",{25,0,0},{10,10,10},0,false}};
    auto context=makeViewPropertiesContext(p);auto first=viewSelectionProperties(context,"A");check(first.at("kind")=="volume","volume properties exist");check(first.at("transport_mass_kg").is_null(),"preview has no fabricated transport mass");check(first.at("material_definition").is_null(),"unpinned preview no material definition");
    Json snapshot={{"schema","scdat.nist-materials/1"},{"provider","Geant4 G4NistManager"},{"engine_version","11.4.3"},{"isotopes_requested",false},{"materials",Json::array({material("G4_Si",2330,14,"Si"),material("G4_Al",2700,13,"Al")})}};
    auto geometry=geometrySnapshot(p);for(auto& v:geometry["volumes"]){double density=v.at("material")=="G4_Si"?2330:2700;v["density_kg_m3"]=density;v["geant4_material_volume_mm3"]=v.at("material_volume_mm3");v["geant4_world_center_mm"]=v.at("world_center_mm");v["geant4_world_rotation"]=v.at("world_rotation");v["mass_kg"]=density*v.at("material_volume_mm3").get<double>()*1e-9;}
    const auto identity=sourceFileIdentity("project.json",QByteArray("synthetic unit fixture"));auto snapshotBytes=QByteArray::fromStdString(snapshot.dump());Json result={{"status","completed"},{"project_sha256",identity.at("sha256")},{"project",p.toJson()},{"engine_version","11.4.3"},{"worker",{{"sha256",std::string(64,'b')}}},{"material_audit",{{"status","resolved_at_run_unpinned"},{"snapshot",snapshot},{"snapshot_sha256",QCryptographicHash::hash(snapshotBytes,QCryptographicHash::Sha256).toHex().toStdString()}}},{"geometry_audit",geometry},{"targets",Json::array({{{"id","A"},{"mass_kg",.00233},{"tid_Gy",.001},{"tid_rad",.1},{"relative_standard_error",.05},{"nonzero_histories",100}}})}};
    auto bind=[&](ViewPropertiesContext& c,const Json& r){bindViewPropertiesResult(c,QByteArray::fromStdString(r.dump()),identity,"result.json");};
    bind(context,result);auto actual=viewSelectionProperties(context,"A");check(actual.at("transport_mass_kg")==.00233,"actual mass from bound audit");check(actual.at("density_kg_m3")==2330,"actual density");check(actual.at("material_definition").at("id")=="G4_Si","material selected by ID");check(actual.at("result").at("tid_Gy")==.001,"dose by stable target ID");check(actual.at("source").at("input")==identity,"raw input identity retained");
    auto changed=p;changed.volumes[0].material="G4_Al";changed.volumes[0].center[0]=7;check(viewSelectionProperties(context,"A")==actual,"editor edits do not affect copied context");
    for(int mode=0;mode<6;++mode){auto bad=result;if(mode==0)bad["project_sha256"]=std::string(64,'c');if(mode==1)bad["geometry_audit"]["volumes"][0]["mass_kg"]=2;if(mode==2)bad["geometry_audit"]["volumes"][0]["material"]="G4_Al";if(mode==3)bad["geometry_audit"]["volumes"][1]["id"]="A";if(mode==4)bad["targets"][0]["id"]="B";if(mode==5)bad["material_audit"]["snapshot"]["materials"][0]["density_kg_m3"]=999;rejects([&]{auto temp=makeViewPropertiesContext(p);bind(temp,bad);},"mismatched result rejected");}
    auto diagnostic=makeViewPropertiesContext(p,true);rejects([&]{bind(diagnostic,result);},"diagnostic cannot bind transport evidence");check(viewSelectionProperties(diagnostic,"A").at("transport_mass_kg").is_null(),"diagnostic no transport mass");
    auto instances=makeViewPropertiesContext(Project::instanceDemo());auto child=viewSelectionProperties(instances,"Unit_B/Chip");check(child.at("instance_origin").at("instance_id")=="Unit_B","instance origin retained");auto assembly=viewSelectionProperties(instances,"Unit_B");check(assembly.at("kind")=="assembly"&&assembly.at("descendant_volume_ids").size()==3,"assembly children and kind");check(assembly.at("transport_mass_kg").is_null(),"coordinate assembly has no invented mass");
    rejects([&]{viewSelectionProperties(context,"missing");},"unknown stable ID rejected");
    auto pinned=p;pinned.materialSnapshot=snapshot;auto pin=viewSelectionProperties(makeViewPropertiesContext(pinned),"A");check(pin.at("estimated_mass_kg")==.00233&&pin.at("transport_mass_kg").is_null(),"pinned estimate kept separate from actual mass");
    auto hide=Project::instanceDemo();hide.assemblies[0].visible=false;auto hidden=viewSelectionProperties(makeViewPropertiesContext(hide),"Unit_B");check(hidden.at("local_visible")==true&&hidden.at("visible")==false,"assembly visibility inherits its parent");
    auto cancelled=result;cancelled["status"]="cancelled";rejects([&]{bind(context,cancelled);},"incomplete result cannot bind scored evidence");check(viewSelectionProperties(context,"A")==actual,"failed binding publishes no partial context");
    auto wrongShape=result;wrongShape["geometry_audit"]["volumes"][0]["shape"]="sphere";rejects([&]{auto temp=makeViewPropertiesContext(p);bind(temp,wrongShape);},"audit shape mismatch rejected, absent legacy box shape accepted");
    std::cout<<count<<" view property checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<count<<": "<<e.what()<<'\n';return 1;}}
