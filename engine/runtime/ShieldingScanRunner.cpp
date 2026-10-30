#include "ShieldingScanRunner.h"
#include "BatchRunner.h"
#include "BatchArchive.h"
#include "ReproReport.h"
#include "RadiationRuntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QThread>
#include <iostream>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Shielding scan: ")+m);}
QByteArray read(const QString& p){QFile f(p);require(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=32*1024*1024,"missing/empty/oversized scan file");auto b=f.readAll();require(f.error()==QFileDevice::NoError,"cannot read scan file");return b;}
QByteArray inside(const QString& root,const QString& rel){
    require(!rel.isEmpty()&&!rel.contains('\\')&&!QDir::isAbsolutePath(rel)&&QDir::cleanPath(rel)==rel&&!rel.startsWith("../"),"unsafe scan file path");
    auto canonical=QFileInfo(root+"/"+rel).canonicalFilePath();
#ifdef Q_OS_WIN
    require(canonical.compare(root+"/"+rel,Qt::CaseInsensitive)==0,"redirected scan artifact");
#else
    require(canonical==root+"/"+rel,"redirected scan artifact");
#endif
    return read(canonical);
}
void raw(const QString& p,const QByteArray& b){QFile f(p);require(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(b)==b.size(),"cannot save new scan file");}
void atomic(const QString& p,const Json& j){for(int i=0;;++i){try{writeResult(p,j);return;}catch(const std::exception&){if(i==19)throw;QThread::msleep(10);}}}
QString casePath(int i){return QString("cases/case-%1").arg(i,6,10,QChar('0'));}
Json recordFor(const ShieldingCase& c){return {{"index",c.index},{"thickness_mm",c.thickness},{"particle",c.particle},{"seed",c.project.seed},
    {"planned_histories",c.project.events},{"expected_primaries",c.project.expectedPrimaries()},{"directory",casePath(c.index).toStdString()},{"status","pending"},{"histories",0},{"converged",false},{"targets",Json::array()}};}
void applyResult(Json& record,const Json& result){record["status"]=result.at("status");record["histories"]=result.at("histories");record["targets"]=result.at("targets");record["converged"]=result.at("batch").at("convergence").at("converged");}
Json freshManifest(const QString& root,const Json& base,const Json& result){auto m=base;m["status"]=result.at("status");m["cases"]=result.at("cases");Json files=Json::array();
    auto add=[&](const QString& name){auto b=inside(root,name);files.push_back({{"path",name.toStdString()},{"size_bytes",b.size()},{"sha256",archiveSha(b)}});};
    for(const char* name:{"project.json","scan-options.json","scan-plan.json","scan-result.json","scan.csv"})add(name);
    for(const auto& row:result.at("cases")){auto path=QString::fromStdString(row.at("directory"));if(QFileInfo::exists(root+"/"+path+"/project.json"))add(path+"/project.json");
        if(QFileInfo::exists(root+"/"+path+"/job/manifest.json"))add(path+"/job/manifest.json");
        if(QFileInfo::exists(root+"/"+path+"/report/package-manifest.json"))add(path+"/report/package-manifest.json");}
    m["files"]=files;return m;
}
}
Json runShieldingScan(const LoadedProject& input,const ShieldingScanOptions& options,const QString& requested,const Json& runtime){
    auto cases=planShieldingScan(input.project,options);auto plan=shieldingPlanJson(input.project,options);auto planBytes=QByteArray::fromStdString(plan.dump(2));
    require(planBytes.size()<=32*1024*1024,"scan plan exceeds 32 MiB");auto root=QFileInfo(requested).absoluteFilePath();
    require(!QFileInfo::exists(root)&&QFileInfo(QFileInfo(root).absolutePath()).isDir(),"scan output must be new under an existing parent");
    require(QDir().mkpath(root),"cannot create scan directory");QLockFile lock(root+"/job.lock");lock.setStaleLockTime(0);require(lock.tryLock(0),"cannot lock scan");
    raw(root+"/project.json",input.bytes);writeResult(root+"/scan-options.json",options.toJson());raw(root+"/scan-plan.json",planBytes);
    Json worker={{"path",QCoreApplication::applicationFilePath().toStdString()},{"sha256",archiveSha(read(QCoreApplication::applicationFilePath()))}};
    Json manifest={{"schema","scdat.radiation-shielding-scan-manifest/1"},{"worker",worker},{"runtime_audit",runtime},
        {"project_sha256",archiveSha(input.bytes)},{"options_sha256",archiveSha(read(root+"/scan-options.json"))},{"plan_sha256",archiveSha(planBytes)}};
    Json result={{"schema","scdat.radiation-shielding-scan-result/1"},{"status","running"},{"error",""},{"worker",worker},{"runtime_audit",runtime},
        {"project_sha256",archiveSha(input.bytes)},{"thicknesses_mm",options.thicknesses},{"cases",Json::array()},{"curves",Json::array()},
        {"geometry_method","Exact 3D hollow box; fixed inner cavity/placement/material, grow outside; zero thickness removes selected shell"},
        {"dose_definition","Target ionizing energy deposition / actual target mass; Gy, rad=100 Gy; source-species exposure applied once"},
        {"uncertainty_rule","Independent incident-species MC variances sum; no paired thickness significance test; zero-hit component makes combined RSE unestimable"},
        {"limitations",Json::array({"Isotropic inward-cosine sphere source; box-shell thickness response is not one-dimensional beam depth or sector approximation",
            "Only incident e- and proton contributions; secondary production stays with its incident species; no charge/material equivalence approximation",
            "Each species/thickness has its own history/time budget and distinct seeds; no mathematical PRNG stream-disjointness proof",
            "No monotonicity assumption, fitted curve, missing-thickness interpolation or external physics certification; zero hits do not mean zero risk"})}};
    for(const auto& c:cases)result["cases"].push_back(recordFor(c));std::vector<Json> results(cases.size(),nullptr);
    auto checkpoint=[&]{auto m=manifest;m["status"]="running";m["cases"]=result.at("cases");atomic(root+"/scan-manifest.json",m);};checkpoint();
    std::string status="completed";for(const auto& c:cases){
        if(QFileInfo::exists(root+"/cancel.request")){status="cancelled";break;}
        auto& row=result["cases"][c.index];row["status"]="running";checkpoint();auto directory=root+"/"+casePath(c.index);
        try{
            require(QDir().mkpath(directory),"cannot create case directory");saveProjectFile(directory+"/project.json",c.project);
            auto loaded=loadProjectFile(directory+"/project.json");
            results[c.index]=runBatchJob(loaded,options.batch,directory+"/job",runtime,root+"/cancel.request");
            applyResult(row,results[c.index]);
            if(row.at("status")=="completed")exportReproReport(directory+"/job",directory+"/report");
            std::cout<<"SCDAT_SCAN_PROGRESS "<<c.index+1<<" "<<cases.size()<<std::endl;
            if(row.at("status")!="completed"){status=row.at("status").get<std::string>();break;}
        }catch(const std::exception& e){row["status"]="failed";status="failed";result["error"]=e.what();break;}
        checkpoint();
    }
    result["status"]=status;result["curves"]=shieldingCurves(cases,results);
    atomic(root+"/scan-result.json",result);raw(root+"/scan.csv",shieldingCsv(result).toUtf8());
    atomic(root+"/scan-manifest.json",freshManifest(root,manifest,result));return result;
}
Json verifyShieldingScan(const QString& requested){
    auto root=QFileInfo(requested).canonicalFilePath();require(QFileInfo(root).isDir(),"scan directory unavailable");
    QLockFile lock(root+"/job.lock");lock.setStaleLockTime(0);require(lock.tryLock(0),"scan is active or locked");
    auto manifest=Json::parse(inside(root,"scan-manifest.json").toStdString());require(manifest.at("schema")=="scdat.radiation-shielding-scan-manifest/1"&&manifest.at("status")!="running","scan must be terminal");
    std::set<std::string> paths;require(manifest.at("files").is_array()&&manifest.at("files").size()<=389,"oversized scan manifest");
    for(const auto& row:manifest.at("files")){auto name=row.at("path").get<std::string>();require(paths.insert(name).second,"duplicate artifact path");auto b=inside(root,QString::fromStdString(name));
        require(b.size()==row.at("size_bytes")&&archiveSha(b)==row.at("sha256"),"scan artifact SHA/size mismatch");}
    for(const char* name:{"project.json","scan-options.json","scan-plan.json","scan-result.json","scan.csv"})require(paths.count(name),"required scan artifact missing");
    auto input=loadProjectFile(root+"/project.json");auto options=ShieldingScanOptions::fromJson(Json::parse(inside(root,"scan-options.json").toStdString()));
    auto plan=planShieldingScan(input.project,options);require(shieldingPlanJson(input.project,options)==Json::parse(inside(root,"scan-plan.json").toStdString()),"scan plan differs from original input/options");
    require(manifest.at("project_sha256")==archiveSha(input.bytes)&&manifest.at("options_sha256")==archiveSha(inside(root,"scan-options.json"))&&manifest.at("plan_sha256")==archiveSha(inside(root,"scan-plan.json")),"manifest source identities differ");
    auto result=Json::parse(inside(root,"scan-result.json").toStdString());require(result.at("schema")=="scdat.radiation-shielding-scan-result/1"&&result.at("cases").size()==plan.size()&&
        result.at("cases")==manifest.at("cases")&&result.at("status")==manifest.at("status")&&result.at("worker")==manifest.at("worker")&&result.at("runtime_audit")==manifest.at("runtime_audit"),"scan summary differs from manifest");
    std::vector<Json> results(plan.size(),nullptr);int complete=0;
    for(const auto& c:plan){auto row=recordFor(c);const auto& actual=result.at("cases").at(c.index);auto rel=casePath(c.index),directory=root+"/"+rel;
        for(const char* key:{"index","thickness_mm","particle","seed","planned_histories","expected_primaries","directory"})require(row.at(key)==actual.at(key),"scan case identity differs");
        auto state=actual.at("status");if(state=="pending"){require(row==actual,"pending case contains results");continue;}
        require(paths.count((rel+"/project.json").toStdString()),"case project missing from manifest");auto loaded=loadProjectFile(directory+"/project.json");
        auto expected=c.project.toJson(),got=loaded.project.toJson();expected.erase("provenance");got.erase("provenance");require(expected==got,"case input model differs");
        require(paths.count((rel+"/job/manifest.json").toStdString()),"case archive manifest missing");auto archive=loadBatchArchive(directory+"/job");
        require(archive.manifest.at("worker_sha256")==manifest.at("worker").at("sha256")&&archive.manifest.at("runtime_audit")==manifest.at("runtime_audit"),"case Worker/runtime differs");
        results[c.index]=Json::parse(inside(directory+"/job","result.json").toStdString());applyResult(row,results[c.index]);require(row==actual,"case summary differs from archived result");
        if(state=="completed"){++complete;require(paths.count((rel+"/report/package-manifest.json").toStdString()),"case report manifest missing");auto report=verifyReproReport(directory+"/report");
            require(report.at("source").at("project_identity").at("sha256")==archive.manifest.at("project_sha256")&&report.at("source").at("result_identity").at("sha256")==archive.manifest.at("result_sha256"),"case report differs from archive");}
    }
    require(result.at("curves")==shieldingCurves(plan,results)&&inside(root,"scan.csv")==shieldingCsv(result).toUtf8(),"combined curves/CSV differ from original transport");
    require(result.at("status")!="completed"||complete==int(plan.size()),"completed scan has pending/partial cases");
    return {{"schema","scdat.radiation-shielding-scan-verification/1"},{"status","verified"},{"cases",plan.size()},{"completed_cases",complete},{"curve_rows",result.at("curves").size()},{"worker_sha256",manifest.at("worker").at("sha256")}};
}
}
