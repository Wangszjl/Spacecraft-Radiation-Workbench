#include "RadiationProject.h"
#include "runtime/GeometryExchange.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
QByteArray readFile(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read temporary file");return f.readAll();}
void writeFile(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw std::runtime_error("cannot write temporary file");}
std::string digest(const Json& value){return QCryptographicHash::hash(QByteArray::fromStdString(value.dump()),QCryptographicHash::Sha256).toHex().toStdString();}
Json sourceIdentity(std::string filename="source-project.json"){
    return {{"file_name",std::move(filename)},{"sha256",std::string(64,'a')},{"size_bytes",1234},{"kind","project_file"}};
}
Json bundleIdentity(){return {{"file_name","analytic-geometry.json"},{"sha256",std::string(64,'b')},{"size_bytes",5678}};}
Json rehash(Json bundle){bundle["model_sha256"]=digest(bundle.at("model"));return bundle;}
Json siSnapshot(){return {{"schema","scdat.nist-materials/1"},{"engine_version","11.4.3"},
    {"provider","Geant4 G4NistManager"},{"isotopes_requested",false},{"materials",Json::array({
        {{"id","G4_Si"},{"density_kg_m3",2330.0},{"state","solid"},{"temperature_K",293.15},{"pressure_Pa",101325.0},
         {"elements",Json::array({{{"name","Silicon"},{"symbol","Si"},{"Z",14},{"A_g_mol",28.0855},{"mass_fraction",1.0}}})}}})}};}
Project schema4Project(){auto p=Project::demo();p.volumes={{"OnlySi","G4_Si",{0,0,0},{20,20,20},0,true}};
    p.assemblies.clear();p.templates.clear();p.instances.clear();p.materialSnapshot=siSnapshot();p.radius=1000;p.validate();return p;}
Project destination(){auto p=Project::demo();p.name="destination task";p.duration=17.5;p.radius=2000;p.events=1234;
    p.seed=98765;p.cut=0.025;p.physics="QGSP_BIC_EMZ";p.environmentSource="destination-spectrum.csv";p.provenance["destination_marker"]="preserved";p.validate();return p;}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        auto two=Project::demo();auto three=Project::assemblyDemo();auto five=Project::instanceDemo();
        auto four=schema4Project();auto six=Project::shapeDemo();
        const std::vector<Project> fixtures{two,three,four,five,six};
        const std::vector<std::string> schemas{"scdat.radiation/2","scdat.radiation/3","scdat.radiation/4","scdat.radiation/5","scdat.radiation/6"};
        const auto base=destination();
        for(std::size_t i=0;i<fixtures.size();++i){
            const auto projectJson=fixtures[i].toJson();
            const auto model=geometryDescriptor(fixtures[i]);
            check(model.at("project_schema")==schemas[i],"Descriptor must retain source schema");
            check(model.at("units")==Json({{"length","mm"},{"energy","MeV_per_particle"},{"time","s"}}),"Descriptor units must be explicit and canonical");
            check(!model.contains("environment")&&!model.contains("duration")&&!model.contains("radius")&&!model.contains("events")&&!model.contains("seed")&&!model.contains("physics"),"Geometry descriptor must exclude transport settings and environment");
            check(Project::fromJson(projectJson).toJson()==projectJson,"Fixture must round-trip through the strict project parser");
            check(geometryDescriptorSha256(fixtures[i])==digest(model),"Descriptor SHA must hash the canonical model bytes");

            const auto src=sourceIdentity("schema-"+std::to_string(i)+".json");
            const auto bundle=exportGeometryBundle(fixtures[i],src);
            check(fixtures[i].toJson()==projectJson,"Export must not mutate the source project");
            check(bundle.at("schema")=="scdat.radiation-geometry/1"&&bundle.at("model")==model&&bundle.at("model_sha256")==digest(model),"Exported bundle schema and model hash must be canonical");
            const auto applied=applyGeometryBundle(base,bundle,bundleIdentity());
            check(geometryDescriptor(applied)==model,"Import must replace the complete geometry model");
            check(applied.name==base.name&&applied.environmentSource==base.environmentSource&&applied.duration==base.duration&&
                  applied.radius==base.radius&&applied.events==base.events&&applied.seed==base.seed&&applied.cut==base.cut&&
                  applied.physics==base.physics&&applied.spectra.size()==base.spectra.size()&&
                  applied.environmentMetadata==base.environmentMetadata,"Import must preserve task environment and transport parameters");
            check(applied.provenance.at("destination_marker")=="preserved"&&applied.provenance.at("geometry_imports").size()==1,
                  "Import provenance must append without replacing destination provenance");
            const auto& history=applied.provenance.at("geometry_imports").back();
            check(history.at("source_project")==src&&history.at("bundle")==bundleIdentity()&&history.at("model_sha256")==bundle.at("model_sha256"),
                  "Import history must retain both source identity and original bundle hash");
        }

        const auto shapeBundle=exportGeometryBundle(six,sourceIdentity());
        auto small=destination();small.radius=100;
        rejects([&]{applyGeometryBundle(small,shapeBundle,bundleIdentity());},"Import must reject geometry outside destination source sphere without enlarging it");
        check(small.radius==100,"Rejected source clearance must not mutate destination");

        auto noTargets=shapeBundle;
        for(auto& v:noTargets["model"]["geometry"]["volumes"])v["sensitive"]=false;
        for(auto& t:noTargets["model"]["geometry"]["templates"])for(auto& v:t["volumes"])v["sensitive"]=false;
        noTargets=rehash(noTargets);
        rejects([&]{applyGeometryBundle(base,noTargets,bundleIdentity());},"Imported geometry without scoring targets must be rejected");

        auto overlap=shapeBundle;auto& volumes=overlap["model"]["geometry"]["volumes"];
        volumes[2]["center_mm"]=volumes[1]["center_mm"];overlap=rehash(overlap);
        rejects([&]{applyGeometryBundle(base,overlap,bundleIdentity());},"Imported overlapping analytic solids must be rejected");

        auto wrongUnits=shapeBundle;wrongUnits["model"]["units"]["length"]="cm";wrongUnits=rehash(wrongUnits);
        rejects([&]{applyGeometryBundle(base,wrongUnits,bundleIdentity());},"Unsupported exchange units must be rejected");
        auto wrongShape=shapeBundle;wrongShape["model"]["geometry"]["volumes"][1]["shape"]="torus";wrongShape=rehash(wrongShape);
        rejects([&]{applyGeometryBundle(base,wrongShape,bundleIdentity());},"Unsupported analytic shape must be rejected");

        auto missing=shapeBundle;missing["model"]["geometry"].erase("volumes");missing=rehash(missing);
        rejects([&]{applyGeometryBundle(base,missing,bundleIdentity());},"Missing canonical geometry field must be rejected");
        auto extra=shapeBundle;extra["model"]["geometry"]["environment"]={{"spectra",Json::array()}};extra=rehash(extra);
        rejects([&]{applyGeometryBundle(base,extra,bundleIdentity());},"Environment payload in geometry bundle must be rejected");
        auto physical=shapeBundle;physical["model"]["geometry"]["radius"]=9999;physical=rehash(physical);
        rejects([&]{applyGeometryBundle(base,physical,bundleIdentity());},"Source-radius payload in geometry bundle must be rejected");
        auto physics=shapeBundle;physics["model"]["geometry"]["physics_list"]="FTFP_BERT";physics=rehash(physics);
        rejects([&]{applyGeometryBundle(base,physics,bundleIdentity());},"Physics payload in geometry bundle must be rejected");
        auto tampered=shapeBundle;tampered["model"]["geometry"]["volumes"][0]["material"]="G4_Cu";
        rejects([&]{applyGeometryBundle(base,tampered,bundleIdentity());},"Model changed without updating SHA must be rejected");
        auto badSource=sourceIdentity();badSource["sha256"]=std::string(64,'A');
        rejects([&]{exportGeometryBundle(six,badSource);},"Uppercase source SHA must be rejected");
        badSource=sourceIdentity();badSource["kind"]="result_file";
        rejects([&]{exportGeometryBundle(six,badSource);},"Unsupported source identity kind must be rejected");
        auto badBundleIdentity=bundleIdentity();badBundleIdentity["size_bytes"]=0;
        rejects([&]{applyGeometryBundle(base,shapeBundle,badBundleIdentity);},"Invalid imported bundle file identity must be rejected");

        QTemporaryDir temporary;check(temporary.isValid(),"Could not create temporary geometry exchange directory");
        const auto bundlePath=temporary.filePath("analytic-bundle.json");
        exportGeometryBundleFile(six,bundlePath,sourceIdentity());
        const auto saved=readFile(bundlePath);
        auto imported=importGeometryBundleFile(base,bundlePath);
        check(geometryDescriptor(imported)==geometryDescriptor(six),"File import/export must preserve the full canonical model");
        check(readFile(bundlePath)==saved,"File import and report must leave original bundle bytes unchanged");
        rejects([&]{exportGeometryBundleFile(six,bundlePath,sourceIdentity());},"Bundle export must refuse to overwrite existing file");
        const auto report=geometryBundleReport(base,bundlePath);
        check(report.at("status")=="ready"&&report.at("project_schema")=="scdat.radiation/6"&&report.at("volume_count")==7,
              "Geometry bundle report must validate and summarize imported model");
        check(readFile(bundlePath)==saved,"Bundle report must not alter source bundle bytes");

        const auto largePath=temporary.filePath("too-large.json");
        writeFile(largePath,QByteArray(10*1024*1024+1,' '));
        rejects([&]{importGeometryBundleFile(base,largePath);},"File import must reject bundles over 10 MiB");
        rejects([&]{geometryBundleReport(base,largePath);},"Bundle report must reject files over 10 MiB");
        const auto oversizedExport=temporary.filePath("oversized-export.json");
        auto hugeSource=sourceIdentity(std::string(10*1024*1024+1,'x'));
        rejects([&]{exportGeometryBundleFile(six,oversizedExport,hugeSource);},"File export must reject bundles over 10 MiB");
        check(!QFile::exists(oversizedExport),"Oversized export must not leave a partial file");

        std::cout<<"Geometry exchange contract: "<<checks<<" checks passed; canonical descriptors, provenance, strict validation and file limits\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
