#include "Materials.h"
#include "Instances.h"
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("Materials: "+message);}
void fields(const Json& row,const std::set<std::string>& names){
    require(row.is_object()&&row.size()==names.size(),"missing/extra snapshot fields");
    for(auto it=row.begin();it!=row.end();++it)require(names.count(it.key())!=0,"unknown field: "+it.key());
}
double number(const Json& row,const char* key,bool zero=false){
    const auto& value=row.at(key);require(value.is_number(),std::string("numeric field required: ")+key);
    double v=value.get<double>();require(std::isfinite(v)&&(zero?v>=0:v>0),std::string("invalid field: ")+key);return v;
}
std::string text(const Json& row,const char* key){
    require(row.at(key).is_string()&&!row.at(key).get<std::string>().empty(),std::string("nonempty text required: ")+key);return row.at(key);
}
}
std::vector<std::string> unassignedMaterialIds(const Project& p){
    auto geometry=expandInstances(p);std::vector<std::string> ids;for(const auto& v:geometry.volumes)if(v.material.empty())ids.push_back(v.id);return ids;
}
void validateMaterialSnapshot(const Project& p){
    const auto& snapshot=p.materialSnapshot;if(snapshot.is_null())return;
    fields(snapshot,{"schema","engine_version","provider","isotopes_requested","materials"});
    require(snapshot.at("schema")=="scdat.nist-materials/1","unsupported snapshot schema");
    require(snapshot.at("provider")=="Geant4 G4NistManager","unsupported material provider");
    require(snapshot.at("isotopes_requested").is_boolean()&&snapshot.at("isotopes_requested")==false,"unsupported isotope construction mode");
    require(std::regex_match(text(snapshot,"engine_version"),std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")),"invalid engine version");
    require(snapshot.at("materials").is_array()&&!snapshot.at("materials").empty(),"empty material definitions");
    std::set<std::string> defined,used;
    auto geometry=expandInstances(p);for(const auto& v:geometry.volumes){require(!v.material.empty(),"unassigned material for volume: "+v.id);used.insert(v.material);}
    for(const auto& m:snapshot.at("materials")){
        fields(m,{"id","density_kg_m3","state","temperature_K","pressure_Pa","elements"});
        auto id=text(m,"id");require(id.rfind("G4_",0)==0&&defined.insert(id).second,"duplicate or invalid material ID: "+id);
        number(m,"density_kg_m3");number(m,"temperature_K");number(m,"pressure_Pa",true);
        auto state=text(m,"state");require(state=="solid"||state=="liquid"||state=="gas"||state=="undefined","invalid material state");
        require(m.at("elements").is_array()&&!m.at("elements").empty(),"empty composition: "+id);
        double sum=0;std::set<int> atomicNumbers;
        for(const auto& e:m.at("elements")){
            fields(e,{"name","symbol","Z","A_g_mol","mass_fraction"});text(e,"name");text(e,"symbol");
            require(e.at("Z").is_number_integer(),"atomic number must be integer");auto z=e.at("Z").get<std::int64_t>();
            require(z>=1&&z<=118&&atomicNumbers.insert(int(z)).second,"duplicate or invalid element Z");
            number(e,"A_g_mol");double fraction=number(e,"mass_fraction");require(fraction<=1,"mass fraction exceeds one");sum+=fraction;
        }
        require(std::abs(sum-1)<=1e-10,"element mass fractions must sum to one: "+id);
    }
    require(defined==used,"snapshot does not exactly cover assigned materials; query and pin materials again");
}
}
