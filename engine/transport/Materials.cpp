#include "Materials.h"
#include "../Materials.h"
#include "Geometry.h"
#include <G4NistManager.hh>
#include <G4Material.hh>
#include <G4Element.hh>
#include <G4SystemOfUnits.hh>
#include <G4Version.hh>
#include <QCryptographicHash>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace spacerad::engine::transport {
namespace {
std::string version(){return std::to_string(G4VERSION_NUMBER/100)+"."+std::to_string((G4VERSION_NUMBER/10)%10)+"."+std::to_string(G4VERSION_NUMBER%10);}
Json definition(const G4Material& material){
    std::string state="undefined";
    switch(material.GetState()){case kStateSolid:state="solid";break;case kStateLiquid:state="liquid";break;case kStateGas:state="gas";break;default:break;}
    std::map<int,Json> elements;const auto* fractions=material.GetFractionVector();
    for(size_t i=0;i<material.GetNumberOfElements();++i){const auto* e=material.GetElementVector()->at(i);
        elements[int(e->GetZ())]={{"name",e->GetName()},{"symbol",e->GetSymbol()},{"Z",int(e->GetZ())},
            {"A_g_mol",e->GetA()/(g/mole)},{"mass_fraction",fractions[i]}};}
    Json rows=Json::array();for(const auto& entry:elements)rows.push_back(entry.second);
    return {{"id",material.GetName()},{"density_kg_m3",material.GetDensity()/(kg/m3)},
        {"state",state},{"temperature_K",material.GetTemperature()/kelvin},{"pressure_Pa",material.GetPressure()/pascal},{"elements",rows}};
}
}
Json nistMaterialNames(){Json names=Json::array();for(const auto& name:G4NistManager::Instance()->GetNistMaterialNames())names.push_back(name);return names;}
Json currentMaterialSnapshot(const Project& p){
    auto* nist=G4NistManager::Instance();const auto& names=nist->GetNistMaterialNames();std::map<std::string,Json> definitions;
    auto geometry=expandInstances(p);for(const auto& v:geometry.volumes){
        if(definitions.count(v.material))continue;
        if(std::find(names.begin(),names.end(),v.material)==names.end())throw std::runtime_error("Unknown NIST material for volume "+v.id+": "+v.material);
        auto* material=nist->FindOrBuildMaterial(v.material,false);
        if(!material)throw std::runtime_error("Cannot construct NIST material for volume "+v.id+": "+v.material);
        definitions[v.material]=definition(*material);
    }
    Json rows=Json::array();for(const auto& entry:definitions)rows.push_back(entry.second);
    Json snapshot={{"schema","scdat.nist-materials/1"},{"engine_version",version()},{"provider","Geant4 G4NistManager"},
        {"isotopes_requested",false},{"materials",rows}};
    auto check=p;check.materialSnapshot=snapshot;validateMaterialSnapshot(check);return snapshot;
}
Json auditMaterialSnapshot(const Project& p){
    auto actual=currentMaterialSnapshot(p);bool pinned=!p.materialSnapshot.is_null();
    if(pinned){
        validateMaterialSnapshot(p);
        if(p.materialSnapshot.at("engine_version")!=actual.at("engine_version"))throw std::runtime_error("Pinned material Geant4 version differs from Worker; explicitly query and pin again");
        // Definitions are canonical, sorted by material ID and atomic number.
        // Compare keyed rows so user serialization order does not change meaning.
        std::map<std::string,Json> expected;for(const auto& row:p.materialSnapshot.at("materials")){
            auto normalized=row;auto& elements=normalized["elements"];
            std::sort(elements.begin(),elements.end(),[](const Json& a,const Json& b){return a.at("Z").get<int>()<b.at("Z").get<int>();});expected[row.at("id").get<std::string>()]=normalized;}
        for(const auto& row:actual.at("materials"))if(expected.at(row.at("id").get<std::string>())!=row)
            throw std::runtime_error("Pinned material definition differs from Worker NIST database: "+row.at("id").get<std::string>());
    }
    auto bytes=QByteArray::fromStdString(actual.dump());
    return {{"status",pinned?"pinned_match":"resolved_at_run_unpinned"},{"snapshot",actual},
        {"snapshot_sha256",QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()},
        {"source","Geant4 built-in NIST reference definitions; version is the actual Worker SDK"},
        {"scope","Reference material definitions, not certification of spacecraft material composition"}};
}
Json inspectMaterials(const Project& p){
    auto audit=auditMaterialSnapshot(p);std::map<std::string,double> density;
    for(const auto& m:audit.at("snapshot").at("materials"))density[m.at("id")]=m.at("density_kg_m3");
    auto geometry=geometrySnapshot(p);Json mappings=Json::array();for(const auto& row:geometry.at("volumes")){
        auto id=row.at("material").get<std::string>();mappings.push_back({{"volume_id",row.at("id")},{"material",id},
            {"sensitive",row.at("sensitive")},{"material_volume_mm3",row.at("material_volume_mm3")},
            {"estimated_mass_kg",row.at("material_volume_mm3").get<double>()*density.at(id)*1e-9}});}
    return {{"schema","scdat.material-report/1"},{"status","ready"},{"material_audit",audit},{"catalog_names",nistMaterialNames()},
        {"assignments",mappings},{"mass_semantics","Analytic pre-transport estimates; scoring uses Geant4 logical-volume mass"}};
}
}
