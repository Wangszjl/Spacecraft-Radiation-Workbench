#include "spacerad/ShieldingScan.h"
#include "spacerad/Results.h"
#include "runtime/ShieldingScan.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QPointer>
#include <QTimer>
#include <cmath>
#include <set>
#include <stdexcept>

namespace spacerad {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string hash(const QByteArray& bytes) { return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString(); }
QByteArray bytes(const Json& json) { return QByteArray::fromStdString(json.dump(2)); }
QByteArray read(const QString& path, qint64 budget=32*1024*1024) {
    QFile file(path);
    require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly) && file.size()>0 && file.size()<=budget,"Missing or oversized scan asset");
    auto value=file.read(budget+1);
    require(file.error()==QFileDevice::NoError && value.size()<=budget && file.atEnd(),"Cannot read bounded scan asset");
    return value;
}
Json parse(const QByteArray& raw) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(raw.toStdString(),[&](int depth,Json::parse_event_t event,Json& value){
        require(depth<=128,"Scan JSON nesting exceeds 128");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate scan JSON key");
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
}
void save(const QString& path,const QByteArray& raw) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(raw)==raw.size() && file.commit(),"Cannot save scan checkpoint");
}
QString casePath(int index) { return QString("cases/case-%1").arg(index,6,10,QChar('0')); }
engine::ShieldingScanOptions options(const Project& p,const ShieldingScanSettings& raw) {
    const auto settings=ShieldingScanSettings::fromJson(raw.toJson());
    engine::ShieldingScanOptions result;
    result.shellId=settings.shellId;result.thicknesses=settings.thicknesses;
    result.batch.historiesPerBatch=p.events;result.batch.maxHistories=p.events;result.batch.maxParallel=1;
    return result;
}
Json assets(const RuntimeOptions& runtime) {
    QFile worker(runtime.worker);
    require(worker.open(QIODevice::ReadOnly),"Cannot fingerprint scan Worker");
    QCryptographicHash sha(QCryptographicHash::Sha256);
    require(sha.addData(&worker),"Cannot read scan Worker");
    const auto dir=QFileInfo(runtime.worker).absoluteDir();
    return {{"worker_sha256",sha.result().toHex().toStdString()},
        {"build_context_sha256",hash(read(dir.filePath("radiation-build.json"),1024*1024))},
        {"manifest_sha256",hash(read(dir.filePath("radiation-datasets.txt"),65536))}};
}
void checkRuntime(const Json& result,const Json& expected) {
    require(result.at("worker").at("sha256")==expected.at("worker_sha256") &&
        result.at("runtime_audit").at("build_context_sha256")==expected.at("build_context_sha256") &&
        result.at("runtime_audit").at("manifest_sha256")==expected.at("manifest_sha256"),"Scan runtime changed between cases");
}
Json curves(const Json& plan,const std::vector<Json>& results) {
    const auto& cases=plan.at("cases");
    require(cases.size()==results.size(),"Scan result slots differ from plan");
    Json rows=Json::array();
    for(size_t first=0;first<cases.size();) {
        size_t end=first+1;while(end<cases.size() && cases[end].at("thickness_mm")==cases[first].at("thickness_mm"))++end;
        bool complete=true;for(size_t i=first;i<end;++i)complete=complete && !results[i].is_null();
        if(complete) for(const auto& target:results[first].at("targets")) {
            Json components=Json::array(),zeros=Json::array();double dose=0,variance=0;
            for(size_t i=first;i<end;++i) {
                const Json* found=nullptr;
                for(const auto& t:results[i].at("targets"))if(t.at("id")==target.at("id"))found=&t;
                require(found && found->at("mass_kg")==target.at("mass_kg"),"Scan target IDs or masses differ between species");
                const double value=found->at("tid_Gy"),error=found->at("tid_standard_error_Gy");
                require(std::isfinite(value) && value>=0 && std::isfinite(error) && error>=0,"Invalid scan dose/error");
                dose+=value;variance+=error*error;
                if(found->at("relative_standard_error").is_null())zeros.push_back(cases[i].at("particle"));
                components.push_back({{"particle",cases[i].at("particle")},{"tid_Gy",value},{"tid_standard_error_Gy",error},
                    {"relative_standard_error",found->at("relative_standard_error")},{"histories",results[i].at("histories")},{"nonzero_histories",found->at("nonzero_histories")}});
            }
            const double error=std::sqrt(variance);
            require(std::isfinite(dose) && std::isfinite(variance) && std::isfinite(dose*100),"Scan sum overflows");
            rows.push_back({{"thickness_mm",cases[first].at("thickness_mm")},{"target_id",target.at("id")},{"mass_kg",target.at("mass_kg")},
                {"tid_Gy",dose},{"tid_rad",dose*100},{"tid_standard_error_Gy",error},{"relative_standard_error",dose>0 && zeros.empty()?Json(error/dose):Json(nullptr)},
                {"zero_hit_components",zeros},{"components",components}});
        }
        first=end;
    }
    return rows;
}
QString csvText(const Json& value) {
    QString text=QString::fromStdString(value.get<std::string>());
    if(!text.isEmpty() && (text.front().isSpace() || QString("=+-@").contains(text.front())))text.prepend('\'');
    text.replace('"',"\"\"");return '"'+text+'"';
}
}
Json ShieldingScanSettings::toJson() const { return {{"schema","spacerad.shielding-settings/1"},{"shell_id",shellId},{"thicknesses_mm",thicknesses}}; }
ShieldingScanSettings ShieldingScanSettings::fromJson(const Json& j) {
    require(j.is_object() && j.size()==3 && j.at("schema")=="spacerad.shielding-settings/1","Unsupported scan settings");
    ShieldingScanSettings result;result.shellId=j.at("shell_id").get<std::string>();
    require(!result.shellId.empty(),"Select a hollow box shell");
    require(j.at("thicknesses_mm").is_array() && j.at("thicknesses_mm").size()>=2 && j.at("thicknesses_mm").size()<=64,"Require 2..64 thicknesses");
    double previous=-1;
    for(const auto& item:j.at("thicknesses_mm")) {
        require(item.is_number(),"Thickness must be numeric");const double t=item.get<double>();
        require(std::isfinite(t) && t>=0 && t<=10000 && t>previous,"Thicknesses must be strictly increasing within 0..10000 mm");
        result.thicknesses.push_back(t);previous=t;
    }
    return result;
}
Json planShieldingScan(const Project& p,const ShieldingScanSettings& settings) {
    auto planned=engine::planShieldingScan(p,options(p,settings));
    Json cases=Json::array();
    for(const auto& c:planned)cases.push_back({{"index",c.index},{"thickness_mm",c.thickness},{"particle",c.particle},
        {"directory",casePath(c.index).toStdString()},{"project",c.project.toJson()}});
    Json result={{"schema","spacerad.shielding-plan/1"},{"settings",settings.toJson()},{"histories_per_case",p.events},
        {"total_planned_histories",std::int64_t(p.events)*planned.size()},{"cases",cases},
        {"geometry_rule","Fixed inner cavity, placement and material; grow the outer box; zero thickness removes the selected shell"}};
    require(bytes(result).size()<=32*1024*1024,"Scan plan exceeds 32 MiB");return result;
}
QByteArray shieldingScanCsv(const Json& report) {
    require(report.at("schema")=="spacerad.shielding-result/1","Unsupported scan result");
    QString csv="thickness_mm,target_id,component,tid_Gy,standard_error_Gy,relative_standard_error,zero_hit_components\n";
    for(const auto& row:report.at("curves")) {
        auto add=[&](const Json& value,const Json& component,const Json& rse){
            csv+=QString::fromStdString(row.at("thickness_mm").dump())+','+csvText(row.at("target_id"))+','+csvText(component)+','+
                QString::fromStdString(value.at("tid_Gy").dump())+','+QString::fromStdString(value.at("tid_standard_error_Gy").dump())+','+
                (rse.is_null()?QString{}:QString::fromStdString(rse.dump()))+','+csvText(row.at("zero_hit_components").dump())+'\n';
        };
        add(row,"total",row.at("relative_standard_error"));
        for(const auto& c:row.at("components"))add(c,c.at("particle"),c.at("relative_standard_error"));
    }
    return csv.toUtf8();
}
struct ShieldingScanService::Impl {
    RuntimeOptions runtime;AnalysisService* child=nullptr;QString state="ready",directory;Json plan,report,identity;
    QByteArray input,settings,accepted;std::vector<Json> results;size_t current=0;quint64 generation=0;
};
ShieldingScanService::ShieldingScanService(QObject* parent):QObject(parent),d(new Impl) {
    d->child=new AnalysisService(this);
    // Deliver outward signals after the leaf service has returned from its own
    // process callbacks; hosts may destroy this service from a signal handler.
    connect(d->child,&AnalysisService::logMessage,this,&ShieldingScanService::logMessage,Qt::QueuedConnection);
    connect(d->child,&AnalysisService::progress,this,[this](int n,int total){if(running())emit progress(int(d->current),int(d->results.size()),n,total);},Qt::QueuedConnection);
    connect(d->child,&AnalysisService::stateChanged,this,[this](const QString& state){if(d->state=="cancelling" && state=="cancelled")finish("cancelled");},Qt::QueuedConnection);
    connect(d->child,&AnalysisService::failed,this,[this](const QString& error){if(running())finish(d->state=="cancelling"?"cancelled":"failed",error);},Qt::QueuedConnection);
    connect(d->child,&AnalysisService::completed,this,[this](const QByteArray& raw){
        if(d->state=="cancelling"){finish("cancelled");return;}
        if(d->state!="running")return;
        try {
            const auto result=parse(raw);checkRuntime(result,d->identity);
            d->results.at(d->current)={{"targets",result.at("targets")},{"histories",result.at("histories")}};
            auto& record=d->report["cases"].at(d->current);record["status"]="completed";record["result_sha256"]=hash(raw);
            ++d->current;checkpoint();QPointer<ShieldingScanService> alive(this);emit progress(int(d->current),int(d->results.size()),0,0);
            if(!alive)return;
            const auto generation=d->generation;
            QTimer::singleShot(0,this,[this,generation]{if(generation==d->generation && d->state=="running")next();});
        }catch(const std::exception& error){finish("failed",QString::fromUtf8(error.what()));}
    },Qt::QueuedConnection);
}
ShieldingScanService::~ShieldingScanService() {
    disconnect(d->child,nullptr,this,nullptr);
    if(running()) {
        d->child->cancel();d->state="cancelled";d->report["status"]="cancelled";
        if(d->current<d->results.size() && d->report["cases"][d->current]["status"]=="running")d->report["cases"][d->current]["status"]="cancelled";
        try { checkpoint(); }catch(...){}
    }
    delete d->child;d->child=nullptr;
}
void ShieldingScanService::configure(const RuntimeOptions& runtime) { require(!running(),"Cannot configure a running scan");d->runtime=runtime;d->accepted.clear();d->state="ready";emit stateChanged(d->state); }
bool ShieldingScanService::running() const { return d->state=="running" || d->state=="cancelling"; }
QString ShieldingScanService::state() const { return d->state; }
QString ShieldingScanService::outputDirectory() const { return d->directory; }
QByteArray ShieldingScanService::result() const { return d->accepted; }
void ShieldingScanService::checkpoint() {
    d->report["curves"]=curves(d->plan,d->results);
    const auto raw=bytes(d->report);
    require(raw.size()<=32*1024*1024,"Scan report exceeds 32 MiB");
    save(d->directory+"/scan-result.json",raw);
    save(d->directory+"/scan.csv",shieldingScanCsv(d->report));
    save(d->directory+"/scan-manifest.json",bytes(Json{{"schema","spacerad.shielding-manifest/1"},{"project_sha256",hash(d->input)},
        {"settings_sha256",hash(d->settings)},{"plan_sha256",hash(bytes(d->plan))},{"result_sha256",hash(raw)}}));
}
void ShieldingScanService::start(const Project& p,const ShieldingScanSettings& settings,const QString& directory) {
    require(!running(),"Scan already running");
    const auto plan=planShieldingScan(p,settings);const auto input=bytes(p.toJson());
    require(input.size()<=8*1024*1024,"Scan project exceeds 8 MiB");
    AnalysisService::childEnvironment(d->runtime);const auto identity=assets(d->runtime);
    require(!directory.trimmed().isEmpty() && QFileInfo(directory).isAbsolute() && !QFileInfo::exists(directory),"Choose a new absolute scan directory");
    require(QDir().mkpath(directory+"/cases"),"Cannot create scan directory");
    d->directory=QFileInfo(directory).absoluteFilePath();d->plan=plan;d->input=input;d->settings=bytes(settings.toJson());d->identity=identity;
    d->results.assign(plan.at("cases").size(),nullptr);d->current=0;++d->generation;d->accepted.clear();
    d->report={{"schema","spacerad.shielding-result/1"},{"status","running"},{"error",""},{"settings",settings.toJson()},
        {"runtime_identity",identity},{"histories_per_case",p.events},{"total_planned_histories",plan.at("total_planned_histories")},
        {"cases",Json::array()},{"curves",Json::array()},{"geometry_rule",plan.at("geometry_rule")},
        {"uncertainty_rule","Sum incident-species doses and MC variances under independent-run assumption; distinct seeds are not a mathematical stream-disjointness proof; no between-thickness significance test"},
        {"limitations",Json::array({"Fixed histories per species/thickness; no adaptive convergence or monotonicity assumption", "Zero-hit species make combined RSE unestimable; zero hits do not mean zero risk", "Synthetic/external spectra; no orbit model, calibrated DDD/SEE or independent dose physics validation"})}};
    for(const auto& c:plan.at("cases")){auto record=c;record.erase("project");record["status"]="pending";d->report["cases"].push_back(record);}
    save(d->directory+"/project.json",input);save(d->directory+"/scan-settings.json",d->settings);save(d->directory+"/scan-plan.json",bytes(plan));checkpoint();
    d->state="running";QPointer<ShieldingScanService> alive(this);emit stateChanged(d->state);
    if(!alive)return;
    const auto generation=d->generation;
    QTimer::singleShot(0,this,[this,generation]{if(generation==d->generation && d->state=="running")next();});
}
void ShieldingScanService::next() {
    if(d->state!="running")return;
    try {
        require(assets(d->runtime)==d->identity,"Scan runtime assets changed");
        if(d->current==d->results.size()){finish("completed");return;}
        const auto& c=d->plan.at("cases").at(d->current);
        d->report["cases"][d->current]["status"]="running";checkpoint();
        d->child->configure(d->runtime);
        d->child->start(Project::fromJson(c.at("project")),d->directory+"/"+casePath(int(d->current)));
    }catch(const std::exception& error){finish("failed",QString::fromUtf8(error.what()));}
}
void ShieldingScanService::finish(const QString& state,const QString& error) {
    if(!running())return;
    d->accepted.clear();d->state=state;d->report["status"]=state.toStdString();d->report["error"]=error.toUtf8().toStdString();
    if(d->current<d->results.size() && d->report["cases"][d->current]["status"]=="running")d->report["cases"][d->current]["status"]=state.toStdString();
    try {checkpoint();if(state=="completed")d->accepted=bytes(loadShieldingScan(d->directory));}
    catch(const std::exception& e){d->state="failed";d->report["status"]="failed";d->report["error"]=e.what();try{checkpoint();}catch(...){}QPointer<ShieldingScanService> alive(this);emit failed(QString::fromUtf8(e.what()));if(alive)emit stateChanged(d->state);return;}
    QPointer<ShieldingScanService> alive(this);
    if(state=="completed")emit completed(d->accepted);
    else if(state=="failed")emit failed(error);
    if(alive)emit stateChanged(d->state);
}
void ShieldingScanService::cancel() {
    if(d->state!="running")return;
    d->state="cancelling";QPointer<ShieldingScanService> alive(this);emit stateChanged(d->state);
    if(!alive)return;
    if(d->child->running())d->child->cancel();else finish("cancelled");
}
Json loadShieldingScan(const QString& directory) {
    require(!directory.trimmed().isEmpty() && QFileInfo(directory).isDir(),"Choose an existing terminal scan directory");
    const auto root=QFileInfo(directory).absoluteFilePath();
    const auto input=read(root+"/project.json",8*1024*1024),settingsRaw=read(root+"/scan-settings.json",65536),planRaw=read(root+"/scan-plan.json"),resultRaw=read(root+"/scan-result.json"),manifestRaw=read(root+"/scan-manifest.json",65536);
    const auto manifest=parse(manifestRaw);auto report=parse(resultRaw);
    require(manifest.at("schema")=="spacerad.shielding-manifest/1" && manifest.at("project_sha256")==hash(input) && manifest.at("settings_sha256")==hash(settingsRaw) &&
        manifest.at("plan_sha256")==hash(planRaw) && manifest.at("result_sha256")==hash(resultRaw),"Scan asset hash mismatch");
    const auto settings=ShieldingScanSettings::fromJson(parse(settingsRaw));
    const auto plan=planShieldingScan(Project::fromJson(parse(input)),settings);
    require(plan==parse(planRaw),"Scan plan differs from saved input/settings");
    require(report.at("schema")=="spacerad.shielding-result/1" && (report.at("status")=="completed" || report.at("status")=="cancelled" || report.at("status")=="failed"),"Scan is not terminal");
    require(report.at("settings")==settings.toJson() && report.at("histories_per_case")==plan.at("histories_per_case") && report.at("total_planned_histories")==plan.at("total_planned_histories") &&
        report.at("cases").is_array() && report.at("cases").size()==plan.at("cases").size(),"Scan summary differs from plan");
    std::vector<Json> results(plan.at("cases").size(),nullptr);size_t completed=0;
    for(size_t i=0;i<results.size();++i) {
        const auto& record=report.at("cases").at(i);const auto& c=plan.at("cases").at(i);
        for(const auto* key:{"index","thickness_mm","particle","directory"})require(record.at(key)==c.at(key),"Scan case identity differs from plan");
        const auto state=record.at("status");
        require(state=="completed" || state=="cancelled" || state=="failed" || state=="pending","Invalid terminal scan case state");
        if(state!="completed")continue;
        const auto childDir=root+"/"+casePath(int(i));const auto child=loadArchivedRun(childDir);
        require(read(childDir+"/project.json",8*1024*1024)==bytes(c.at("project")) && record.at("result_sha256")==child.summary().at("result_sha256"),"Scan child differs from planned snapshot");
        checkRuntime(child.result(),report.at("runtime_identity"));results[i]={{"targets",child.result().at("targets")},{"histories",child.result().at("histories")}};++completed;
    }
    require(report.at("status")!="completed" || completed==results.size(),"Completed scan contains unfinished cases");
    require(report.at("curves")==curves(plan,results) && read(root+"/scan.csv")==shieldingScanCsv(report),"Scan curves/CSV differ from archived transport");
    require(read(root+"/scan-result.json")==resultRaw && read(root+"/scan-manifest.json",65536)==manifestRaw,"Scan changed while loading");
    return report;
}
}
