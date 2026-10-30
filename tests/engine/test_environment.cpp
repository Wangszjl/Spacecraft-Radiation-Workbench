#include "RadiationProject.h"
#include "runtime/ProjectFile.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QFile>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid environment accepted");}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Read failed");return f.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw std::runtime_error("Write failed");}
Json legacy(){
    // Real P0 contract, deliberately not constructed by the v2 serializer.
    return {{"schema","scdat.radiation/1"},{"name","Legacy mission"},{"events",3000},{"seed",476291},
        {"duration_s",8},{"source_radius_mm",100},{"production_cut_mm",0.05},{"physics_list","FTFP_BERT_EMZ"},
        {"environment",{{"source","SYNTHETIC v1 fixture"},{"synthetic",true},
            {"flux_convention","omnidirectional_differential_cm-2_s-1_MeV-1"},
            {"spectra",Json::array({{{"particle","proton"},{"energy_MeV",{50,100}},{"differential_flux",{2,2}}}})}}},
        {"volumes",Json::array({{{"id","stable-Si"},{"material","G4_Si"},{"center_mm",{0,0,0}},{"size_mm",{80,80,10}},
            {"wall_mm",0},{"sensitive",true}}})}};
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        auto v1=legacy();auto p=Project::fromJson(v1);auto v2=p.toJson();
        check(v2.at("schema")=="scdat.radiation/2","Schema migration missing");
        check(v2.at("volumes")==v1.at("volumes"),"Stable IDs/geometry changed during migration");
        for(const auto* key:{"name","events","seed","duration_s","source_radius_mm","production_cut_mm","physics_list"})
            check(v2.at(key)==v1.at(key),"Legacy configuration changed");
        check(v2.at("environment").at("metadata")==defaultEnvironmentMetadata(),"Missing metadata must remain null");
        check(Project::fromJson(v2).toJson()==v2,"V2 roundtrip drift");
        auto fluence=p.convertedQuantity(SpectrumQuantity::Fluence);
        check(fluence.expectedPrimaries()==p.expectedPrimaries(),"Flux to fluence changes exposure");
        check(fluence.spectra[0].values[0]==16,"Fluence must integrate flux once");
        double expectation=fluence.expectedPrimaries();fluence.duration=80;
        check(fluence.expectedPrimaries()==expectation,"Fluence incorrectly multiplied by duration");
        fluence.duration=8;auto restored=fluence.convertedQuantity(SpectrumQuantity::Flux);
        check(restored.spectra[0].values==p.spectra[0].values,"Quantity conversion roundtrip");
        check(restored.expectedPrimaries()==p.expectedPrimaries(),"Conversion normalization drift");
        check(Project::fromJson(fluence.toJson()).expectedPrimaries()==fluence.expectedPrimaries(),"Valid fluence roundtrip rejected");
        check(restored.spectra[0].provenance.at("conversions").size()==2,"Quantity conversion history lost");
        rejects([&]{auto j=v1;j["units"]={{"energy","keV"}};Project::fromJson(j);});
        rejects([&]{auto j=v1;j["environment"]["quantity"]="differential_fluence";Project::fromJson(j);});
        auto q=p;q.duration=16;check(q.expectedPrimaries()==2*p.expectedPrimaries(),"Flux duration scaling");
        rejects([&]{auto j=v2;j["units"]["energy"]="keV";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["quantity"]="integral_flux";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["quantity"]="differential_fluence";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["angular_distribution"]="directional";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["schema"]="scdat.radiation/99";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["events"]=4294970296LL;Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"].erase("unit");Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["percentile"]=101;Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["confidence_level"]=-1;Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["model"]="";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["epoch_utc"]="2026-02-30T00:00:00Z";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["epoch_utc"]="2026-10-19T12:00:00+08:00";Project::fromJson(j);});
        rejects([&]{auto j=v2;j["environment"]["metadata"]["interval_start_utc"]="2026-10-20T00:00:00Z";
            j["environment"]["metadata"]["interval_end_utc"]="2026-10-19T00:00:00Z";Project::fromJson(j);});
        q=p;q.environmentMetadata["epoch_utc"]="2024-02-29T12:00:00Z";q.validate();check(true,"Leap date rejected");
        q.environmentMetadata={{"model","Synthetic contract fixture"},{"model_version","fixture-v1"},
            {"orbit_source","No orbit: synthetic test only"},{"epoch_utc","2024-02-29T12:00:00Z"},
            {"interval_start_utc","2024-02-29T12:00:00Z"},{"interval_end_utc","2024-02-29T12:00:08Z"},
            {"statistical_mode","Synthetic nominal fixture"},{"percentile",50},{"confidence_level",95}};
        check(Project::fromJson(q.toJson()).environmentMetadata==q.environmentMetadata,"Structured metadata roundtrip drift");
        check(q.missingEnvironmentMetadata().empty(),"Provided metadata incorrectly reported missing");
        rejects([&]{auto x=p;x.spectra[0].values[0]*=2;x.validate();});
        QTemporaryDir folder;check(folder.isValid(),"Temporary directory unavailable");
        QString input=folder.path()+QString::fromUtf8("/旧工程.json"),output=folder.path()+"/v2.json";
        auto original=QByteArray::fromStdString(v1.dump(2));write(input,original);
        auto loaded=loadProjectFile(input);
        auto hash=QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex().toStdString();
        check(loaded.project.provenance.at("migration_origin").at("sha256")==hash,"Migration hash must describe raw source bytes");
        saveProjectFile(output,loaded.project);
        check(read(input)==original,"Migration modified original file");
        auto outputBefore=read(output);rejects([&]{saveProjectFile(output,loaded.project);});
        check(read(output)==outputBefore,"Rejected overwrite damaged output");
        rejects([&]{saveProjectFile(input,loaded.project);});check(read(input)==original,"Same-path migration damaged original");
        auto reopened=loadProjectFile(output);
        check(reopened.project.provenance.at("migration_origin").at("sha256")==hash,"Reopening lost original migration source");
        check(reopened.project.volumes[0].id=="stable-Si","Reopening changed stable volume ID");
        auto csv=folder.path()+QString::fromUtf8("/原始谱.csv");
        write(csv,"\xEF\xBB\xBF" "energy_MeV,differential_fluence\n1,2\n3,2\n");
        auto source=loadSpectrumCsv(csv,"proton",true,SpectrumQuantity::Fluence);
        check(std::abs(source.integral-16*pi)<1e-12,"Per-sr conversion must occur once");
        check(source.provenance.at("source_file").at("sha256")==QCryptographicHash::hash(read(csv),QCryptographicHash::Sha256).toHex().toStdString(),"CSV raw hash mismatch");
        check(source.provenance.at("conversions").size()==1,"Angular conversion history missing");
        rejects([&]{loadSpectrumCsv(csv,"proton",false,SpectrumQuantity::Flux);});
        rejects([&]{parseSpectrumCsv("energy_MeV,differential_flux\n1,2,\n2,3\n","proton",false,SpectrumQuantity::Flux);});
        rejects([&]{parseSpectrumCsv("energy_keV,differential_flux\n1,2\n2,3\n","proton",false,SpectrumQuantity::Flux);});
        rejects([&]{auto x=p;x.spectra[0].provenance["conversions"].push_back({{"operation","quantity_conversion"},{"factor",8}});x.validate();});
        std::cout<<"Environment contract: "<<checks<<" checks passed; v1/v2, quantity semantics, metadata, provenance and file preservation\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
