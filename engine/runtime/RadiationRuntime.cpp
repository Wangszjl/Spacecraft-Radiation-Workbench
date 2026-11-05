#include "RadiationRuntime.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <G4Version.hh>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
std::string engineVersion() {
    return std::to_string(G4VERSION_NUMBER/100)+"."+std::to_string((G4VERSION_NUMBER/10)%10)+"."+std::to_string(G4VERSION_NUMBER%10);
}
}
Json auditRuntime() {
    QFile build(QCoreApplication::applicationDirPath()+"/radiation-build.json");
    if(!build.open(QIODevice::ReadOnly))throw std::runtime_error("Worker build configuration snapshot missing; rebuild worker");
    auto buildBytes=build.readAll();
    if(QCryptographicHash::hash(buildBytes,QCryptographicHash::Sha256).toHex()!=SCDAT_BUILD_CONTEXT_SHA256)
        throw std::runtime_error("Worker build configuration snapshot hash mismatch");
    auto context=Json::parse(buildBytes.toStdString());
    if(context.at("compiled_geant4_version")!=engineVersion())throw std::runtime_error("Worker compiled package/header version mismatch");
    QFile manifest(QCoreApplication::applicationDirPath()+"/radiation-datasets.txt");
    if(!manifest.open(QIODevice::ReadOnly)) throw std::runtime_error("Worker dataset manifest missing");
    auto bytes=manifest.readAll();
    Json datasets=Json::object(),checks=Json::array();
    std::set<std::string> names;
    for(const auto& raw:bytes.split('\n')) {
        auto line=raw.trimmed();if(line.isEmpty())continue;
        auto fields=line.split('|');
        if(fields.size()!=2 || fields[0].isEmpty() || fields[1].isEmpty())
            throw std::runtime_error("Malformed worker dataset manifest");
        auto name=fields[0].toStdString();
        if(!names.insert(name).second) throw std::runtime_error("Duplicate dataset manifest entry: "+name);
        auto actual=QString::fromLocal8Bit(qgetenv(fields[0].constData()));
        auto expectedName=QFileInfo(QString::fromUtf8(fields[1])).fileName();
        if(actual.isEmpty()) throw std::runtime_error("Required dataset environment variable missing: "+name);
        QFileInfo info(QDir::cleanPath(actual));
        if(!info.isDir() || !info.isReadable()) throw std::runtime_error("Dataset directory missing/unreadable: "+name);
        if(info.fileName().compare(expectedName,Qt::CaseInsensitive)!=0)
            throw std::runtime_error("Dataset directory version/name mismatch: "+name+"; expected "+expectedName.toStdString());
        if(QDir(actual).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty())
            throw std::runtime_error("Dataset directory is empty: "+name);
        datasets[name]=info.absoluteFilePath().toStdString();
        checks.push_back({{"envvar",name},{"expected_directory",expectedName.toStdString()},{"directory",info.absoluteFilePath().toStdString()},
            {"check","directory name/version, readable and nonempty; content checksum not verified"}});
    }
    for(const auto* mandatory:{"G4LEDATA","G4PARTICLEXSDATA","G4LEVELGAMMADATA","G4ENSDFSTATEDATA","G4RADIOACTIVEDATA"})
        if(!names.count(mandatory)) throw std::runtime_error(std::string("Incomplete worker dataset manifest: ")+mandatory);
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=SCDAT_DATASET_MANIFEST_SHA256)
        throw std::runtime_error("Worker dataset manifest hash mismatch; use the manifest generated for this executable");
    return {{"schema","scdat.radiation-runtime/1"},{"status","ready"},{"engine",G4Version},{"engine_version",engineVersion()},
        {"dataset_environment",datasets},{"dataset_checks",checks},
        {"manifest_sha256",QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()},
        {"build_context_sha256",QCryptographicHash::hash(buildBytes,QCryptographicHash::Sha256).toHex().toStdString()},
        {"build_configuration",context},
        {"scope","Package/header version and dataset configuration audit; no experiment or full content certification"}};
}
Json addProvenance(Json result,const Project& project,const QByteArray& input,const QString& started,const Json& runtime) {
    QFile executable(QCoreApplication::applicationFilePath());if(!executable.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot fingerprint running Worker");
    auto executableBytes=executable.readAll();result["worker"]={{"path",QCoreApplication::applicationFilePath().toStdString()},
        {"sha256",QCryptographicHash::hash(executableBytes,QCryptographicHash::Sha256).toHex().toStdString()},{"size_bytes",executableBytes.size()}};
    result["schema"]="scdat.radiation-result/2";
    result["engine"]=runtime.at("engine");result["engine_version"]=runtime.at("engine_version");
    result["dataset_environment"]=runtime.at("dataset_environment");result["runtime_audit"]=runtime;
    result["project_sha256"]=QCryptographicHash::hash(input,QCryptographicHash::Sha256).toHex().toStdString();
    result["started_utc"]=started.toStdString();result["finished_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    result["project"]=project.toJson();
    result["spectrum_quantity"]=quantityName(project.quantity);result["spectrum_unit"]=quantityUnit(project.quantity);
    result["environment_metadata_missing"]=project.missingEnvironmentMetadata();
    result["requested_configuration"]={{"physics_list",project.physics},{"production_cut_mm",project.cut},
        {"events",project.events},{"seed",project.seed},{"duration_s",project.duration}};
    result["effective_configuration"]={{"physics_list",project.physics},{"production_cut_mm",project.cut},
        {"histories",result.at("histories")},{"seed",project.seed},{"threading","serial"},
        {"time_multiplier_applied",project.quantity==SpectrumQuantity::Flux}};
    result["capability_status"]={{"tid","research_software_numerical_checks_only"},{"external_physics_validation","not_completed"},
        {"ddd","not_implemented"},{"see","not_implemented"},{"environment_model_execution","not_implemented"}};
    if(!project.sourceSampling.is_null()){
        result["requested_configuration"]["source_sampling"]=project.sourceSampling;
        result["effective_configuration"]["source_sampling"]=project.sourceSampling;
        result["capability_status"]["source_importance"]="implemented_weighted_primary_energy";
    }
    result["validation_status"]={{"software_regression","See version-specific regression evidence"},
        {"external_physics_validation","not_completed"},{"environment",project.synthetic?"synthetic":"user_supplied_not_certified"}};
    result["limitations"]={"Forward MC, serial; boxes, hollow boxes, solid cylinders and spheres; no curved shells or CAD transport","NIEL deposit is not calibrated device DDD or SEE failure probability",
        "Uncertainty is MC standard error only; zero hits do not establish an upper bound","Synthetic environment is not mission radiation assessment"};
    return result;
}
void writeResult(const QString& path,const Json& j) {
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot create output file");
    auto bytes=j.dump(2);
    if(f.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size()))
        throw std::runtime_error("Output write failed ("+std::to_string(bytes.size())+" bytes): "+f.errorString().toStdString());
    if(!f.commit())throw std::runtime_error("Output commit failed ("+std::to_string(bytes.size())+" bytes): "+f.errorString().toStdString());
}
}
