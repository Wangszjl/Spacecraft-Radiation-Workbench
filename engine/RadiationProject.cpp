#include "RadiationProject.h"
#include "Voxels.h"
#include "Geometry.h"
#include "Materials.h"
#include "Instances.h"
#include <spacerad/engine/SourceSampling.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <regex>

namespace spacerad::engine {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
bool positive(double x) { return std::isfinite(x) && x > 0; }
void geometryFields(const Json& row,const std::set<std::string>& allowed){
    require(row.is_object(),"Geometry node must be an object");
    for(auto it=row.begin();it!=row.end();++it)require(allowed.count(it.key())!=0,"Unsupported geometry field: "+it.key());
}
void validateArtifact(const Json& artifact) {
    if(artifact.is_null())return;
    require(artifact.is_object(),"Source file provenance must be an object or null");
    require(artifact.at("file_name").is_string()&&!artifact.at("file_name").get<std::string>().empty(),"Source file_name is required");
    require(artifact.at("size_bytes").is_number_integer()&&artifact.at("size_bytes").get<std::int64_t>()>=0,"Source size_bytes must be a nonnegative integer");
    require(artifact.at("sha256").is_string()&&std::regex_match(artifact.at("sha256").get<std::string>(),std::regex("[a-f0-9]{64}")),"Source SHA-256 must be 64 lowercase hexadecimal characters");
    if(artifact.contains("bytes_base64"))require(artifact.at("bytes_base64").is_string(),"Embedded source bytes must be base64 text");
}
void validateProvenance(const Json& p) {
    require(p.is_object(),"Project provenance must be an object");
    require(p.at("origin_schema")=="scdat.radiation/1"||p.at("origin_schema")=="scdat.radiation/2"||p.at("origin_schema")=="scdat.radiation/3"||p.at("origin_schema")=="scdat.radiation/4"||p.at("origin_schema")=="scdat.radiation/5"||p.at("origin_schema")=="scdat.radiation/6"||p.at("origin_schema")=="scdat.radiation/7"||p.at("origin_schema")=="scdat.radiation/8","Invalid provenance origin_schema");
    validateArtifact(p.at("last_loaded_input"));validateArtifact(p.at("migration_origin"));
}
bool utcTimestamp(const std::string& value) {
    if(!std::regex_match(value,std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")))return false;
    int y=std::stoi(value.substr(0,4)),m=std::stoi(value.substr(5,2)),d=std::stoi(value.substr(8,2));
    if(y<1||m<1||m>12)return false;
    int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y%4==0&&(y%100!=0||y%400==0))days[1]=29;
    return d>=1&&d<=days[m-1]&&std::stoi(value.substr(11,2))<24&&std::stoi(value.substr(14,2))<60&&std::stoi(value.substr(17,2))<60;
}
void validateMetadata(const Json& metadata) {
    const auto defaults=defaultEnvironmentMetadata();
    require(metadata.is_object(),"environment.metadata must be an object");
    for(auto it=metadata.begin();it!=metadata.end();++it)require(defaults.contains(it.key()),"Unknown environment metadata field: "+it.key());
    for(auto it=defaults.begin();it!=defaults.end();++it){
        const auto& v=metadata.at(it.key());if(v.is_null())continue;
        if(it.key()=="percentile"||it.key()=="confidence_level"){
            require(v.is_number()&&std::isfinite(v.get<double>())&&v.get<double>()>=0&&v.get<double>()<=100,"Metadata percentage must be 0..100: "+it.key());
        }else{
            require(v.is_string()&&!v.get<std::string>().empty(),"Metadata must be nonempty text or null: "+it.key());
            if(it.key().find("_utc")!=std::string::npos)require(utcTimestamp(v.get<std::string>()),"Invalid UTC metadata timestamp (YYYY-MM-DDTHH:MM:SSZ): "+it.key());
        }
    }
    const auto& start=metadata.at("interval_start_utc");const auto& end=metadata.at("interval_end_utc");
    if(!start.is_null()&&!end.is_null())require(start.get<std::string>()<=end.get<std::string>(),"Environment interval ends before it starts");
}
}
std::string quantityName(SpectrumQuantity q){return q==SpectrumQuantity::Flux?"differential_flux":"differential_fluence";}
std::string quantityUnit(SpectrumQuantity q){return q==SpectrumQuantity::Flux?"cm-2 s-1 MeV-1":"cm-2 MeV-1";}
Json defaultEnvironmentMetadata(){return {{"model",nullptr},{"model_version",nullptr},{"orbit_source",nullptr},
    {"epoch_utc",nullptr},{"interval_start_utc",nullptr},{"interval_end_utc",nullptr},{"statistical_mode",nullptr},
    {"percentile",nullptr},{"confidence_level",nullptr}};}
Json defaultProjectProvenance(){return {{"origin_schema","scdat.radiation/2"},{"last_loaded_input",nullptr},{"migration_origin",nullptr}};}
void Spectrum::prepare() {
    require(particle == "e-" || particle == "proton" || particle == "alpha" ||
            particle == "gamma" || particle == "neutron", "Unsupported particle: " + particle);
    require(interpolation=="linear"||interpolation=="bin_constant","Unsupported spectrum interpolation");
    bool bins=interpolation=="bin_constant";
    require(energy.size() >= 2 && energy.size() == values.size()+(bins?1:0), "Spectrum needs matching energy points or bin edges/values");
    integral = 0;
    cumulative.assign(energy.size(), 0);
    for (size_t i=0; i<energy.size(); ++i) {
        require(positive(energy[i]) && (i>=values.size()||(std::isfinite(values[i]) && values[i] >= 0)),
                "Energy must be positive; differential flux must be finite and nonnegative");
        if (i) {
            require(energy[i] > energy[i-1], "Spectrum energies must be strictly increasing");
            integral += bins?(energy[i]-energy[i-1])*values[i-1]:(energy[i]-energy[i-1]) * (values[i]+values[i-1]) * 0.5;
            cumulative[i] = integral;
        }
    }
    require(positive(integral), "Spectrum integral must be positive and finite");
}
double Spectrum::sample(double u) const {
    require(std::isfinite(u) && u >= 0 && u < 1, "Sampling value outside [0,1)");
    double area = u * integral;
    if(interpolation=="bin_constant")area=std::min(area,std::nextafter(integral,0.0));
    size_t i = std::upper_bound(cumulative.begin(), cumulative.end(), area) - cumulative.begin();
    i = std::min(std::max(size_t(1), i), energy.size()-1);
    double a = area-cumulative[i-1], width = energy[i]-energy[i-1];
    if(interpolation=="bin_constant")return energy[i-1]+std::clamp(a/values[i-1],0.0,width);
    double slope = (values[i]-values[i-1])/width;
    double disc = std::max(0.0, values[i-1]*values[i-1]+2*slope*a);
    double denom = values[i-1]+std::sqrt(disc);
    double dx = denom > 0 ? 2*a/denom : 0;
    return energy[i-1]+std::clamp(dx, 0.0, width);
}
double Project::integratedSpectrum() const {
    double f=0; for (const auto& s : spectra) f+=s.integral; return f;
}
double Project::expectedPrimaries() const {
    double incident=pi*std::pow(radius/10.0,2)*integratedSpectrum();
    return quantity==SpectrumQuantity::Flux?incident*duration:incident;
}
Project Project::convertedQuantity(SpectrumQuantity target) const {
    validate();auto p=*this;if(target==quantity)return p;
    double factor=target==SpectrumQuantity::Fluence?duration:1.0/duration;
    for(auto& spectrum:p.spectra){
        for(auto& value:spectrum.values)value*=factor;
        spectrum.provenance["conversions"].push_back({{"operation","quantity_conversion"},{"from",quantityName(quantity)},
            {"to",quantityName(target)},{"duration_s",duration},{"factor",factor}});
        spectrum.prepare();
    }
    p.quantity=target;p.validate();return p;
}
std::vector<std::string> Project::missingEnvironmentMetadata() const {
    std::vector<std::string> missing;
    for(auto it=environmentMetadata.begin();it!=environmentMetadata.end();++it)if(it.value().is_null())missing.push_back(it.key());
    return missing;
}
void Project::validate() const {
    require(!name.empty() && !environmentSource.empty(), "Project name and environment provenance are required");
    require(positive(duration) && positive(radius) && positive(cut), "Exposure, source radius and cut must be positive");
    require(events >= 2 && events <= 100000000 && seed > 0 && seed <= 2147483646,
            "Events must be 2..100000000; seed must be 1..2147483646");
    require(physics == "FTFP_BERT_EMZ" || physics == "QGSP_BIC_EMZ", "Unsupported physics list");
    require(quantity==SpectrumQuantity::Flux||quantity==SpectrumQuantity::Fluence,"Unsupported spectrum quantity");
    validateMetadata(environmentMetadata);validateProvenance(provenance);
    require(!spectra.empty() && positive(integratedSpectrum()) && positive(expectedPrimaries()), "Invalid spectrum normalization");
    for (const auto& s : spectra) {
        auto copy=s;copy.prepare();
        require(copy.integral==s.integral&&copy.cumulative==s.cumulative,"Spectrum values changed without rebuilding sampling distribution");
        require(s.provenance.is_object()&&s.provenance.at("conversions").is_array(),"Invalid spectrum provenance");
        validateArtifact(s.provenance.at("source_file"));
        if(s.provenance.contains("import_mapping")){
            const auto& m=s.provenance.at("import_mapping");require(m.is_object(),"Invalid import mapping");
            for(const auto* k:{"block","row"})require(m.at(k).is_number_integer()&&m.at(k).get<std::int64_t>()>=0&&m.at(k).get<std::int64_t>()<=2147483647,"Invalid import mapping index");
            for(const auto* k:{"variable","input_unit"})require(m.at(k).is_string(),"Invalid import mapping description");
        }
        for(const auto& c:s.provenance.at("conversions")){
            require(c.is_object()&&c.contains("operation")&&c.at("operation").is_string(),"Conversion operation is required");
            const auto operation=c.at("operation").get<std::string>();
            require(c.contains("factor")&&c.at("factor").is_number()&&positive(c.at("factor").get<double>()),"Conversion factor must be positive");
            if(operation=="quantity_conversion"){
                const auto from=c.at("from"),to=c.at("to");
                require((from=="differential_flux"&&to=="differential_fluence")||(from=="differential_fluence"&&to=="differential_flux"),"Invalid quantity conversion history");
                require(c.at("duration_s").is_number()&&positive(c.at("duration_s").get<double>()),"Invalid conversion reference duration");
            }else if(operation=="integral_to_bin_average"){
                for(const auto* key:{"retained_integral","excluded_above_last_threshold"})
                    require(c.at(key).is_number()&&std::isfinite(c.at(key).get<double>())&&c.at(key).get<double>()>=0,"Invalid integral conversion summary");
            }else require(operation=="isotropic_per_sr_to_omnidirectional","Unknown conversion operation");
        }
    }
    validateMaterialSnapshot(*this);
    validateGeometry(*this);
    validateVoxels(*this);
    if(!sourceSampling.is_null()){
        require(voxelScoring.is_null(),"Weighted sampling is not supported with voxel scoring");
        const auto sampling=ImportanceSampling::fromJson(sourceSampling);
        std::set<std::string> species;
        for(const auto& spectrum:spectra)require(species.insert(spectrum.particle).second,"Weighted source species must be unique");
        if(!sampling.proposalSpecies.empty()){
            std::set<std::string> configured;
            for(const auto& row:sampling.proposalSpecies)configured.insert(row.first);
            require(configured==species,"Importance proposal must name every incident species exactly once");
        }
    }
}
Project Project::fromJson(const Json& j) {
    bool legacy=j.at("schema")=="scdat.radiation/1";
    bool voxels=j.at("schema")=="scdat.radiation/7";
    bool importance=j.at("schema")=="scdat.radiation/8";
    bool analytic=j.at("schema")=="scdat.radiation/6"||voxels||importance;
    require(importance?j.contains("source_sampling")&&!j.at("source_sampling").is_null():!j.contains("source_sampling"),"Source importance sampling requires project schema v8");
    require(voxels?j.contains("voxel_scoring")&&!j.at("voxel_scoring").is_null():!j.contains("voxel_scoring"),"Voxel scoring requires project schema v7");
    bool instanced=j.at("schema")=="scdat.radiation/5"||analytic;
    if(instanced)require(j.at("instance_contract")=="scdat.component-instances/1"&&j.at("templates").is_array()&&j.at("instances").is_array(),"Unsupported instance contract or node arrays");
    else require(!j.contains("instance_contract")&&!j.contains("templates")&&!j.contains("instances"),"Templates and instances require project schema v5");
    bool pinned=j.at("schema")=="scdat.radiation/4"||(instanced&&(j.contains("material_contract")||j.contains("material_snapshot")));
    bool assembly=j.at("schema")=="scdat.radiation/3"||pinned||instanced;
    if(pinned)require(j.at("material_contract")=="scdat.nist-materials/1"&&!j.at("material_snapshot").is_null(),"v4 requires pinned material snapshot");
    else require(!j.contains("material_contract")&&!j.contains("material_snapshot"),"Material snapshots require project schema v4");
    require(legacy||j.at("schema")=="scdat.radiation/2"||assembly, "Unsupported project schema");
    if(assembly)require(j.at("geometry_contract")== (analytic?"scdat.analytic-assembly/1":"scdat.box-assembly/1"), "Unsupported geometry contract");
    else require(!j.contains("assemblies")&&!j.contains("geometry_contract"), "Assembly geometry requires project schema v3");
    require(j.at("events").is_number_integer() && j.at("seed").is_number_integer(), "Events and seed must be integers");
    require(j.at("events").get<std::int64_t>()>=2&&j.at("events").get<std::int64_t>()<=100000000,"Events outside 2..100000000");
    require(j.at("seed").get<std::int64_t>()>=1&&j.at("seed").get<std::int64_t>()<=2147483646,"Seed outside 1..2147483646");
    Project p;
    if(importance)p.sourceSampling=j.at("source_sampling");
    if(voxels)p.voxelScoring=j.at("voxel_scoring");
    if(pinned)p.materialSnapshot=j.at("material_snapshot");
    if(instanced){
        for(const auto& t:j.at("templates")){
            if(!analytic)require(t.at("schema")=="scdat.component-template/1","Template v2 requires project v6");
            p.templates.push_back(templateFromJson(t));}
        for(const auto& i:j.at("instances"))p.instances.push_back(instanceFromJson(i));
        require(analytic||!p.templates.empty(),"v5 requires component templates");
    }
    p.name=j.at("name"); p.environmentSource=j.at("environment").at("source");
    p.synthetic=j.at("environment").at("synthetic");
    const auto& env=j.at("environment");
    if(legacy){
        require(!env.contains("quantity")&&!env.contains("unit"),"Do not mix v2 quantity/unit fields into a v1 environment");
        if(j.contains("units"))require(j.at("units")==Json({{"length","mm"},{"energy","MeV_per_particle"},{"time","s"}}),"Unsupported units declared in legacy project");
        require(env.at("flux_convention")=="omnidirectional_differential_cm-2_s-1_MeV-1",
                "Input requires omni differential scalar flux; convert per-sr flux by 4*pi, never integral flux");
        p.provenance["origin_schema"]="scdat.radiation/1";
    }else{
        require(j.at("units")==Json({{"length","mm"},{"energy","MeV_per_particle"},{"time","s"}}),"Unsupported project units");
        if(env.at("quantity")=="differential_flux")p.quantity=SpectrumQuantity::Flux;
        else if(env.at("quantity")=="differential_fluence")p.quantity=SpectrumQuantity::Fluence;
        else throw std::runtime_error("Unsupported environment.quantity (integral spectra are not accepted)");
        require(env.at("unit")==quantityUnit(p.quantity),"Spectrum quantity and unit do not match");
        require(env.at("angular_distribution")=="isotropic"&&env.at("angular_convention")=="omnidirectional_scalar","Only isotropic omnidirectional scalar spectra are supported");
        require(!env.contains("flux_convention"),"Legacy flux_convention is not accepted in schema v2");
        auto metadata=env.at("metadata");require(metadata.is_object(),"environment.metadata must be an object");
        for(auto it=metadata.begin();it!=metadata.end();++it)p.environmentMetadata[it.key()]=it.value();
        p.provenance=j.at("provenance");
    }
    p.duration=j.at("duration_s"); p.radius=j.at("source_radius_mm");
    p.events=j.at("events"); p.seed=j.at("seed"); p.cut=j.at("production_cut_mm"); p.physics=j.at("physics_list");
    if(assembly){
        require(j.at("assemblies").is_array(),"assemblies must be an array");
        for(const auto& a:j.at("assemblies")){
            geometryFields(a,{"id","parent","center_mm","rotation_deg","visible"});
            p.assemblies.push_back({a.at("id"),a.at("parent"),a.at("center_mm").get<std::array<double,3>>(),a.at("rotation_deg").get<std::array<double,3>>(),a.at("visible")});
        }
    }
    for(const auto& v:j.at("volumes")){
        if(!analytic)require(!v.contains("shape"),"Shape tags require project schema v6");
        Volume volume{v.at("id"),v.at("material"),v.at("center_mm").get<std::array<double,3>>(),
            v.at("size_mm").get<std::array<double,3>>(),v.at("wall_mm"),v.at("sensitive")};
        if(assembly){auto allowed=std::set<std::string>{"id","material","center_mm","size_mm","wall_mm","sensitive","parent","rotation_deg","visible"};if(analytic)allowed.insert("shape");geometryFields(v,allowed);
            volume.parent=v.at("parent");volume.rotation=v.at("rotation_deg").get<std::array<double,3>>();volume.visible=v.at("visible");}
        else require(!v.contains("parent")&&!v.contains("rotation_deg")&&!v.contains("visible"),"Volume transforms/visibility require project schema v3");
        if(analytic)volume.shape=v.at("shape");
        p.volumes.push_back(volume);
    }
    for(const auto& s:j.at("environment").at("spectra")) {
        Spectrum sp; sp.particle=s.at("particle"); sp.energy=s.at("energy_MeV").get<std::vector<double>>();
        sp.values=s.at(legacy?"differential_flux":"values").get<std::vector<double>>();
        if(!legacy){require(!s.contains("differential_flux"),"Use values in schema v2 spectra");sp.provenance=s.at("provenance");sp.interpolation=s.value("interpolation","linear");}
        sp.prepare(); p.spectra.push_back(sp);
    }
    p.validate(); return p;
}
Json Project::toJson() const {
    validate();
    bool analytic=hasAnalyticGeometry(*this)||!voxelScoring.is_null()||!sourceSampling.is_null();bool instanced=!templates.empty()||!instances.empty()||analytic;
    bool pinned=!materialSnapshot.is_null();bool assembly=hasAssemblyGeometry(*this)||pinned||instanced;
    Json j={{"schema",!sourceSampling.is_null()?"scdat.radiation/8":!voxelScoring.is_null()?"scdat.radiation/7":analytic?"scdat.radiation/6":instanced?"scdat.radiation/5":pinned?"scdat.radiation/4":assembly?"scdat.radiation/3":"scdat.radiation/2"},{"name",name},{"duration_s",duration},
        {"source_radius_mm",radius},{"events",events},{"seed",seed},{"production_cut_mm",cut},{"physics_list",physics}};
    j["units"]={{"length","mm"},{"energy","MeV_per_particle"},{"time","s"}};j["provenance"]=provenance;
    if(!voxelScoring.is_null())j["voxel_scoring"]=voxelScoring;
    if(!sourceSampling.is_null())j["source_sampling"]=sourceSampling;
    if(instanced){j["instance_contract"]="scdat.component-instances/1";j["templates"]=Json::array();j["instances"]=Json::array();
        for(const auto& t:templates)j["templates"].push_back(templateToJson(t));for(const auto& i:instances)j["instances"].push_back(instanceToJson(i));}
    if(pinned){j["material_contract"]="scdat.nist-materials/1";j["material_snapshot"]=materialSnapshot;}
    j["volumes"]=Json::array();
    if(assembly){
        j["geometry_contract"]=analytic?"scdat.analytic-assembly/1":"scdat.box-assembly/1";j["assemblies"]=Json::array();
        for(const auto& a:assemblies)j["assemblies"].push_back({{"id",a.id},{"parent",a.parent},{"center_mm",a.center},{"rotation_deg",a.rotation},{"visible",a.visible}});
    }
    for(const auto& v:volumes){
        Json row={{"id",v.id},{"material",v.material},{"center_mm",v.center},{"size_mm",v.size},{"wall_mm",v.wall},{"sensitive",v.sensitive}};
        if(assembly){row["parent"]=v.parent;row["rotation_deg"]=v.rotation;row["visible"]=v.visible;}
        if(analytic)row["shape"]=v.shape;
        j["volumes"].push_back(row);
    }
    j["environment"]={{"source",environmentSource},{"synthetic",synthetic},
        {"quantity",quantityName(quantity)},{"unit",quantityUnit(quantity)},{"angular_distribution","isotropic"},
        {"angular_convention","omnidirectional_scalar"},{"metadata",environmentMetadata},{"spectra",Json::array()}};
    for(const auto& s:spectra){
        Json row={{"particle",s.particle},{"energy_MeV",s.energy},{"values",s.values},{"provenance",s.provenance}};
        if(s.interpolation!="linear")row["interpolation"]=s.interpolation;
        j["environment"]["spectra"].push_back(row);
    }
    return j;
}
Project Project::demo() {
    Project p; p.name="Spacecraft radiation demonstrator";
    p.environmentSource="SYNTHETIC engineering pipeline example; not an AE9/AP9 prediction";
    p.volumes={ {"Spacecraft_Al","G4_Al",{0,0,0},{600,500,400},3,false},
        {"Electronics_box","G4_Al",{0,0,0},{220,180,100},2,false},
        {"OBC_Si","G4_Si",{-45,0,0},{50,50,2},0,true},
        {"Payload_Si","G4_Si",{50,15,5},{40,40,2},0,true},
        {"Solar_cell_Si","G4_Si",{0,0,220},{160,100,1},0,true} };
    Spectrum s; s.particle="proton"; s.energy={10,30,100,300}; s.values={100,35,8,1}; s.prepare();
    p.spectra.push_back(s); p.validate(); return p;
}
Project Project::assemblyDemo(){
    auto p=demo();p.name="Hierarchical spacecraft box assembly (synthetic)";p.radius=750;
    p.provenance["origin_schema"]="scdat.radiation/3";
    p.assemblies={{"Spacecraft","",{25,-20,10},{10,15,25},true},
        {"Avionics","Spacecraft",{-50,0,0},{0,0,20},true},
        {"Board_A","Avionics",{0,0,-20},{0,0,0},true},
        {"Payload","Spacecraft",{150,60,0},{0,20,-15},true},
        {"Solar","Spacecraft",{430,0,0},{0,30,0},true}};
    p.volumes={{"Bus_Al","G4_Al",{0,0,0},{600,500,400},3,false,"Spacecraft"},
        {"Deck_Al","G4_Al",{0,0,-160},{590,490,4},0,false,"Spacecraft"},
        {"Avionics_box_Al","G4_Al",{0,0,0},{180,140,100},2,false,"Avionics"},
        {"Board_A_substrate","G4_SILICON_DIOXIDE",{0,0,-2},{140,100,1.6},0,false,"Board_A"},
        {"OBC_Si","G4_Si",{-30,0,0},{30,30,1},0,true,"Board_A"},
        {"Memory_Si","G4_Si",{30,0,0},{25,25,1},0,true,"Board_A"},
        {"Payload_box_Al","G4_Al",{0,0,0},{100,100,80},2,false,"Payload"},
        {"Payload_Si","G4_Si",{0,0,0},{40,40,2},0,true,"Payload"},
        {"Solar_panel_Al","G4_Al",{0,0,0},{180,300,2},0,false,"Solar"},
        {"Solar_cell_Si","G4_Si",{0,0,2},{160,280,0.3},0,true,"Solar"}};
    p.validate();return p;
}
Project Project::instanceDemo(){
    auto p=demo();p.name="Reusable avionics instances (synthetic)";p.radius=750;p.provenance["origin_schema"]="scdat.radiation/5";
    p.assemblies={{"Spacecraft","",{25,-20,10},{10,15,25},true}};
    p.volumes={{"Bus_Al","G4_Al",{0,0,0},{600,500,400},3,false,"Spacecraft"},
        {"Deck_Al","G4_Al",{0,0,-160},{590,490,4},0,false,"Spacecraft"}};
    ComponentTemplate unit;unit.id="AvionicsUnit";unit.assemblies={{"Board","",{0,0,-15},{0,0,10},true}};
    unit.volumes={{"Housing","G4_Al",{0,0,0},{130,100,80},2,false},
        {"Substrate","G4_SILICON_DIOXIDE",{0,0,-2},{90,65,1.6},0,false,"Board"},
        {"Chip","G4_Si",{0,0,0},{30,30,1},0,true,"Board"}};
    p.templates={unit};p.instances={{"Unit_A","AvionicsUnit","Spacecraft",{-130,-60,0},{0,0,15},true,{}},
        {"Unit_B","AvionicsUnit","Spacecraft",{130,60,0},{0,10,-20},true,{{"Housing","G4_Cu"}}}};
    p.validate();return p;
}
Project Project::shapeDemo(){
    auto p=demo();p.name="Analytic cylinders and spheres (synthetic)";p.radius=600;p.provenance["origin_schema"]="scdat.radiation/6";
    p.assemblies={{"Spacecraft","",{20,-15,10},{12,18,25},true}};
    p.volumes={{"Bus_Al","G4_Al",{0,0,0},{500,400,360},3,false,"Spacecraft"},
        {"Tank_Al","G4_Al",{-140,60,0},{65,65,65},0,false,"Spacecraft",{},true,"sphere"},
        {"Cylinder_Si","G4_Si",{110,-65,0},{70,70,35},0,true,"Spacecraft",{25,35,15},true,"cylinder"}};
    ComponentTemplate sensor;sensor.id="RoundSensor";
    sensor.volumes={{"Ball_Si","G4_Si",{0,0,0},{45,45,45},0,true,"",{},true,"sphere"},
        {"Mount_Al","G4_Al",{0,0,-30},{60,60,8},0,false,"",{},true,"cylinder"}};
    p.templates={sensor};p.instances={{"Sensor_A","RoundSensor","Spacecraft",{-70,-70,30},{0,30,15},true,{}},
        {"Sensor_B","RoundSensor","Spacecraft",{90,85,20},{20,0,-30},true,{}}};
    p.validate();return p;
}
Spectrum integralSpectrum(const std::vector<double>& energy,const std::vector<double>& above,
                          const std::string& particle,bool perSr,SpectrumQuantity quantity){
    require(energy.size()>=2&&above.size()==energy.size(),"Integral spectrum needs matching >=2 thresholds and values");
    Spectrum s;s.particle=particle;s.interpolation="bin_constant";s.energy=energy;
    const double factor=perSr?4*pi:1;
    for(size_t i=0;i<energy.size();++i){
        require(positive(energy[i])&&std::isfinite(above[i])&&above[i]>=0,"Invalid integral threshold or value");
        if(i){
            require(energy[i]>energy[i-1],"Integral energy thresholds must strictly increase");
            require(above[i]<=above[i-1],"Above-threshold integral spectrum must not increase");
            s.values.push_back((above[i-1]-above[i])/(energy[i]-energy[i-1])*factor);
        }
    }
    s.prepare();
    if(perSr)s.provenance["conversions"].push_back({{"operation","isotropic_per_sr_to_omnidirectional"},{"factor",4*pi}});
    s.provenance["conversions"].push_back({{"operation","integral_to_bin_average"},{"factor",1},
        {"input_quantity",quantity==SpectrumQuantity::Flux?"integral_flux":"integral_fluence"},
        {"method","adjacent above-threshold differences / linear energy-bin width; constant density per bin"},
        {"retained_integral",s.integral},{"excluded_above_last_threshold",above.back()*factor},
        {"input_thresholds_MeV",energy},{"input_above_threshold",above},
        {"coverage","Only [first,last] threshold interval; below-first and above-last energies not transported"}});
    return s;
}
Spectrum readSpectrumCsv(const std::string& path,const std::string& particle,bool perSr,SpectrumQuantity quantity) {
    std::ifstream in(std::filesystem::u8path(path)); require(bool(in), "Cannot open spectrum CSV");
    std::ostringstream bytes;bytes<<in.rdbuf();require(!in.bad(),"Cannot read spectrum CSV");
    return parseSpectrumCsv(bytes.str(),particle,perSr,quantity);
}
Spectrum parseSpectrumCsv(const std::string& bytes,const std::string& particle,bool perSr,SpectrumQuantity quantity) {
    std::istringstream in(bytes);
    Spectrum s; s.particle=particle;
    std::string line; size_t number=0;
    while(std::getline(in,line)) {
        ++number;
        if(number==1 && line.rfind("\xEF\xBB\xBF",0)==0) line.erase(0,3);
        if(line.find_first_not_of(" \t\r")==std::string::npos || line[line.find_first_not_of(" \t\r")]=='#') continue;
        if(s.energy.empty()&&line.rfind("energy_MeV,",0)==0){
            auto header=line;while(!header.empty()&&(header.back()=='\r'||header.back()==' '||header.back()=='\t'))header.pop_back();
            require(header=="energy_MeV,"+quantityName(quantity),"CSV header does not match selected spectrum quantity");continue;
        }
        require(std::count(line.begin(),line.end(),',')<=1,"Spectrum CSV must have exactly two columns");
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line); double e,f; std::string extra;
        require(bool(row>>e>>f) && !(row>>extra), "Invalid spectrum CSV at line "+std::to_string(number));
        s.energy.push_back(e); s.values.push_back(f*(perSr?4*pi:1));
    }
    s.prepare(); return s;
}
void Moments::add(double x) {
    ++count; if(x>0) ++nonzero;
    double d=x-mean; mean+=d/count; m2+=d*(x-mean);
}
double Moments::standardError() const { return count>1?std::sqrt(std::max(0.0,m2)/(count-1)/count):0; }
}
