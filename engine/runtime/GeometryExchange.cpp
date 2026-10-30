#include "GeometryExchange.h"
#include "ProjectFile.h"
#include "Geometry.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <regex>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
constexpr qint64 maximumBytes=10*1024*1024;
const std::set<std::string> geometryKeys={"volumes","assemblies","geometry_contract","templates","instances","instance_contract","material_contract","material_snapshot"};
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error("Geometry exchange: "+message);}
void fields(const Json& object,const std::set<std::string>& keys){
    require(object.is_object()&&object.size()==keys.size(),"missing or extra fields");
    for(auto it=object.begin();it!=object.end();++it)require(keys.count(it.key()),"unsupported field: "+it.key());
}
void identity(const Json& value,bool source){
    auto keys=std::set<std::string>{"file_name","sha256","size_bytes"};if(source)keys.insert("kind");fields(value,keys);
    require(value.at("file_name").is_string()&&!value.at("file_name").get<std::string>().empty(),"file name required");
    require(value.at("size_bytes").is_number_integer()&&value.at("size_bytes").get<std::int64_t>()>0,"source size must be a positive integer");
    require(value.at("sha256").is_string()&&std::regex_match(value.at("sha256").get<std::string>(),std::regex("[a-f0-9]{64}")),"invalid SHA-256 identity");
    if(source)require(value.at("kind")=="project_file"||value.at("kind")=="workbench_snapshot","unknown source identity kind");
}
std::string digest(const Json& value){auto bytes=QByteArray::fromStdString(value.dump());return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();}
QByteArray readBundle(const QString& path){
    QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()<=maximumBytes,"cannot read geometry JSON <=10 MiB");
    auto bytes=f.read(maximumBytes+1);require(f.error()==QFileDevice::NoError&&!bytes.isEmpty()&&bytes.size()<=maximumBytes,"geometry bundle empty, unreadable or too large");return bytes;
}
void validateBundle(const Json& bundle){
    fields(bundle,{"schema","source_project","model","model_sha256"});
    require(bundle.at("schema")=="scdat.radiation-geometry/1","unsupported geometry bundle schema");identity(bundle.at("source_project"),true);
    const auto& model=bundle.at("model");fields(model,{"project_schema","units","geometry"});
    require(model.at("units")==Json({{"length","mm"},{"energy","MeV_per_particle"},{"time","s"}}),"unsupported model units");
    require(model.at("project_schema").is_string(),"project schema must be text");
    const auto schema=model.at("project_schema").get<std::string>();
    require(schema=="scdat.radiation/2"||schema=="scdat.radiation/3"||schema=="scdat.radiation/4"||schema=="scdat.radiation/5"||schema=="scdat.radiation/6","unsupported geometry source schema");
    const auto& geometry=model.at("geometry");require(geometry.is_object(),"geometry must be an object");
    for(auto it=geometry.begin();it!=geometry.end();++it)require(geometryKeys.count(it.key()),"non-geometry field in geometry payload: "+it.key());
    require(bundle.at("model_sha256").is_string()&&bundle.at("model_sha256")==digest(model),"model SHA-256 mismatch");
}
}
Json geometryDescriptor(const Project& p){
    auto j=p.toJson();Json geometry=Json::object();for(const auto& key:geometryKeys)if(j.contains(key))geometry[key]=j.at(key);
    return {{"project_schema",j.at("schema")},{"units",j.at("units")},{"geometry",geometry}};
}
std::string geometryDescriptorSha256(const Project& p){return digest(geometryDescriptor(p));}
Json exportGeometryBundle(const Project& p,const Json& source){
    identity(source,true);auto model=geometryDescriptor(p);
    return {{"schema","scdat.radiation-geometry/1"},{"source_project",source},{"model",model},{"model_sha256",digest(model)}};
}
Project applyGeometryBundle(const Project& base,const Json& bundle,const Json& bundleIdentity){
    validateBundle(bundle);identity(bundleIdentity,false);auto j=base.toJson();const auto& model=bundle.at("model");
    auto before=geometryDescriptorSha256(base);
    for(const auto& key:geometryKeys)j.erase(key);
    for(auto it=model.at("geometry").begin();it!=model.at("geometry").end();++it)j[it.key()]=it.value();
    j["schema"]=model.at("project_schema");j["units"]=model.at("units");
    // Reuse the same strict shape/assembly/template/material parser, source
    // clearance, overlap and scoring-target validation as ordinary projects.
    // The explicit base supplies every environment and transport parameter.
    auto candidate=Project::fromJson(j);
    require(geometryDescriptor(candidate)==model,"geometry is not canonical or contains unsupported fields");
    auto& history=candidate.provenance["geometry_imports"];
    if(history.is_null())history=Json::array();require(history.is_array(),"geometry_imports provenance must be an array");
    history.push_back({{"operation","replace_geometry"},{"bundle",bundleIdentity},{"source_project",bundle.at("source_project")},
        {"model_sha256",bundle.at("model_sha256")},{"previous_model_sha256",before}});
    candidate.validate();return candidate;
}
Project importGeometryBundleFile(const Project& base,const QString& path){
    auto bytes=readBundle(path);return applyGeometryBundle(base,Json::parse(bytes.toStdString()),sourceFileIdentity(path,bytes));
}
void exportGeometryBundleFile(const Project& p,const QString& path,const Json& source){
    require(!QFile::exists(path),"refusing to overwrite an existing geometry bundle");
    auto raw=exportGeometryBundle(p,source).dump(2);require(raw.size()<=maximumBytes,"export exceeds 10 MiB geometry limit");
    QSaveFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(raw.data(),qint64(raw.size()))==qint64(raw.size())&&f.commit(),"cannot atomically save geometry bundle");
}
Json geometryBundleReport(const Project& base,const QString& path){
    auto p=importGeometryBundleFile(base,path);auto geometry=compileGeometry(p);
    return {{"schema","scdat.geometry-exchange-report/1"},{"status","ready"},
        {"geometry_import",p.provenance.at("geometry_imports").back()},{"project_schema",p.toJson().at("schema")},
        {"assembly_count",geometry.assemblies.size()},{"volume_count",geometry.volumes.size()},
        {"template_count",p.templates.size()},{"instance_count",p.instances.size()},
        {"materials_pinned",!p.materialSnapshot.is_null()},{"geometry_audit",geometrySnapshot(p)},
        {"scope","CPU model and destination source-clearance validation; SDK definitions checked separately before transport"}};
}
}
