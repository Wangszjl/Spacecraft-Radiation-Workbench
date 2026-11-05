#include "ProjectFile.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <stdexcept>
namespace spacerad::engine {
Json sourceFileIdentity(const QString& path,const QByteArray& bytes,bool embed){
    Json identity={{"file_name",QFileInfo(path).fileName().toUtf8().toStdString()},
        {"sha256",QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()},
        {"size_bytes",bytes.size()}};
    if(embed)identity["bytes_base64"]=bytes.toBase64().toStdString();
    return identity;
}
namespace {
QByteArray readFile(const QString& path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open input file: "+path.toUtf8().toStdString());
    auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError)throw std::runtime_error("Cannot read input file");return bytes;
}
}
LoadedProject loadProjectFile(const QString& path,bool refreshMaterialSnapshot){
    auto bytes=readFile(path);auto input=Json::parse(bytes.toStdString());
    auto view=input;
    if(refreshMaterialSnapshot&&(input.at("schema")=="scdat.radiation/4"||input.at("schema")=="scdat.radiation/5"||input.at("schema")=="scdat.radiation/6"||input.at("schema")=="scdat.radiation/7"||input.at("schema")=="scdat.radiation/8")&&input.contains("material_contract")&&input.at("material_contract")=="scdat.nist-materials/1"){
        if(input.at("schema")=="scdat.radiation/4")view["schema"]="scdat.radiation/3";
        view.erase("material_contract");view.erase("material_snapshot");
    }
    auto p=Project::fromJson(view);auto identity=sourceFileIdentity(path,bytes);
    for(const auto& spectrum:p.spectra){
        const auto& source=spectrum.provenance.at("source_file");
        if(!source.is_null()&&source.contains("bytes_base64")){
            auto encoded=QByteArray::fromStdString(source.at("bytes_base64").get<std::string>());
            auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
            if(!decoded||decoded.decoded.size()!=source.at("size_bytes").get<qint64>()||
                QCryptographicHash::hash(decoded.decoded,QCryptographicHash::Sha256).toHex().toStdString()!=source.at("sha256").get<std::string>())
                throw std::runtime_error("Embedded original spectrum bytes do not match file identity");
        }
    }
    p.provenance["last_loaded_input"]=identity;
    if(input.at("schema")=="scdat.radiation/1")p.provenance["migration_origin"]=identity;
    p.validate();return {std::move(p),bytes,input.at("schema").get<std::string>()};
}
Spectrum loadSpectrumCsv(const QString& path,const std::string& particle,bool perSr,SpectrumQuantity quantity,bool integral){
    auto before=readFile(path);
    if(before.size()>10*1024*1024)throw std::runtime_error("Spectrum file exceeds 10 MiB import limit");
    auto parseBytes=before;
    if(integral){
        const QByteArray header=quantity==SpectrumQuantity::Flux?"energy_MeV,integral_flux":"energy_MeV,integral_fluence";
        auto lines=before.split('\n');bool found=false;
        for(auto line:lines){if(line.startsWith("\xEF\xBB\xBF"))line.remove(0,3);line=line.trimmed();
            if(line.isEmpty()||line.startsWith('#'))continue;
            if(line!=header)throw std::runtime_error("Integral CSV requires exact energy_MeV,integral_flux or integral_fluence header matching selected quantity");
            found=true;break;
        }
        if(!found)throw std::runtime_error("Integral CSV header missing");
        parseBytes.replace(header,quantity==SpectrumQuantity::Flux?"energy_MeV,differential_flux":"energy_MeV,differential_fluence");
    }
    auto spectrum=parseSpectrumCsv(parseBytes.toStdString(),particle,integral?false:perSr,quantity);
    if(integral)spectrum=integralSpectrum(spectrum.energy,spectrum.values,particle,perSr,quantity);
    spectrum.provenance["source_file"]=sourceFileIdentity(path,before,true);
    spectrum.provenance["original_quantity"]=integral?(quantity==SpectrumQuantity::Flux?"integral_flux":"integral_fluence"):quantityName(quantity);
    spectrum.provenance["original_unit"]=(integral?(quantity==SpectrumQuantity::Flux?std::string("cm-2 s-1"):std::string("cm-2")):quantityUnit(quantity))+(perSr?" sr-1":"");
    spectrum.provenance["original_angular_convention"]=perSr?"per_steradian":"omnidirectional_scalar";
    if(perSr&&!integral)spectrum.provenance["conversions"].push_back({{"operation","isotropic_per_sr_to_omnidirectional"},{"factor",4*pi}});
    return spectrum;
}
void saveProjectFile(const QString& path,const Project& project,bool allowOverwrite){
    auto bytes=project.toJson().dump(2);
    if(!allowOverwrite&&QFile::exists(path))throw std::runtime_error("Refusing to overwrite an existing project; choose a new output path");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size())||!file.commit())
        throw std::runtime_error("Project atomic save failed");
}
}
