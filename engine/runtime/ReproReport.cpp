#include "ReproReport.h"
#include "ReportHtml.h"
#include "VoxelStatistics.h"
#include "Geometry.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
constexpr qint64 fileLimit=32*1024*1024,totalLimit=256*1024*1024;
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Repro report: ")+m);}
QByteArray readBounded(const QString& p){QFile f(p);require(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=fileLimit,"missing, empty or oversized report file (32 MiB)");
    auto b=f.read(fileLimit+1);require(f.error()==QFileDevice::NoError&&b.size()<=fileLimit,"report read failed");return b;}
QByteArray inside(const QString& root,const QString& relative){
    require(!relative.isEmpty()&&!relative.contains('\\')&&!QDir::isAbsolutePath(relative)&&QDir::cleanPath(relative)==relative&&!relative.startsWith("../"),"unsafe report path");
    auto absolute=root+"/"+relative,canonical=QFileInfo(absolute).canonicalFilePath();
#ifdef Q_OS_WIN
    require(canonical.compare(absolute,Qt::CaseInsensitive)==0,"redirected report path");
#else
    require(canonical==absolute,"redirected report path");
#endif
    return readBounded(absolute);
}
void writeNew(const QString& path,const QByteArray& b){require(!QFileInfo::exists(path)&&b.size()<=fileLimit&&!b.isEmpty(),"refusing existing, empty or oversized package file");
    require(QDir().mkpath(QFileInfo(path).absolutePath()),"cannot create report subdirectory");QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit(),"cannot commit report file");}
QByteArray jsonBytes(const Json& j){return QByteArray::fromStdString(j.dump(2));}
bool shaValid(const std::string& value){return std::regex_match(value,std::regex("[a-f0-9]{64}"));}
bool near(double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-12*std::max({std::abs(a),std::abs(b),1e-288});}
bool numbersEqual(const Json& a,const Json& b){
    if(a.is_number()&&b.is_number())return near(a.get<double>(),b.get<double>());
    if(a.type()!=b.type())return a==b;
    if(a.is_object()){if(a.size()!=b.size())return false;for(auto it=a.begin();it!=a.end();++it)if(!b.contains(it.key())||!numbersEqual(it.value(),b.at(it.key())))return false;return true;}
    if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!numbersEqual(a[i],b[i]))return false;return true;}
    return a==b;
}
bool placementEqual(const Json& a,const Json& b){
    if(a.is_number()&&b.is_number()){double x=a.get<double>(),y=b.get<double>();return std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-9;}
    if(!a.is_array()||!b.is_array()||a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!placementEqual(a[i],b[i]))return false;return true;
}
Json noProvenance(Json p){p.erase("provenance");return p;}
struct Source {
    QString directory;LoadedProject input;Json result,archiveIdentity=nullptr;BatchOptions options;std::vector<int> indices;
    std::string workerSha;std::map<QString,QByteArray> files;std::unique_ptr<BatchArchive> archive;
};
Source loadSource(const QString& request){
    Source s;s.directory=QFileInfo(request).canonicalFilePath();require(QFileInfo(s.directory).isDir(),"job directory unavailable");
    if(QFileInfo::exists(s.directory+"/batches/manifest.json"))s.directory=QFileInfo(s.directory+"/batches").canonicalFilePath();
    bool batch=QFileInfo::exists(s.directory+"/manifest.json");
    if(batch){s.archive=std::make_unique<BatchArchive>(loadBatchArchive(s.directory));require(s.archive->manifest.at("status")!="running","report requires a terminal task; recover the interrupted checkpoint first");
        s.archiveIdentity=s.archive->identity;s.input=s.archive->input;s.options=s.archive->options;s.workerSha=s.archive->manifest.at("worker_sha256").get<std::string>();
        for(const char* name:{"project.json","result.json","options.json","plan.json","manifest.json"})s.files[QString::fromLatin1(name)]=inside(s.directory,QString::fromLatin1(name));
        for(const auto& child:s.archive->completed){auto relative=QString::fromStdString(child.record.at("directory"));
            s.files[relative+"/project.json"]=child.projectBytes;s.files[relative+"/result.json"]=child.resultBytes;s.files[relative+"/transport.log"]=child.logBytes;
            s.indices.push_back(child.record.at("index"));}
    }else{
        s.files["project.json"]=inside(s.directory,"project.json");s.files["result.json"]=inside(s.directory,"result.json");
        s.input=loadProjectFile(s.directory+"/project.json");require(s.input.bytes==s.files.at("project.json"),"source input changed while reading");
        if(QFileInfo::exists(s.directory+"/transport.log"))s.files["transport.log"]=inside(s.directory,"transport.log");
        s.options.maxHistories=s.input.project.events;s.options.historiesPerBatch=s.input.project.events;s.options.maxParallel=1;
    }
    s.result=Json::parse(s.files.at("result.json").toStdString());const auto& r=s.result;
    require(r.at("schema")=="scdat.radiation-result/2"&&r.at("project_sha256")==archiveSha(s.files.at("project.json")),"result schema or exact input SHA mismatch");
    require(r.at("status")=="completed"||r.at("status")=="cancelled"||r.at("status")=="failed","unsupported task state");
    require(noProvenance(Project::fromJson(r.at("project")).toJson())==noProvenance(s.input.project.toJson()),"embedded result model differs from actual input");
    require(r.at("runtime_audit").is_object()&&r.at("targets").is_array(),"runtime or target audit missing");
    if(!batch){require(r.at("status")=="completed"&&r.at("histories")==s.input.project.events,"serial task is incomplete");
        auto pooled=mergeBatchResults(s.input.project,{r});require(compareReportReplay(r,pooled).at("passed")==true,"serial score fields disagree with event moments");s.indices={0};
        if(r.contains("worker"))s.workerSha=r.at("worker").at("sha256").get<std::string>();
    }else{
        std::vector<Json> childResults;for(const auto& c:s.archive->completed)childResults.push_back(c.result);
        if(!childResults.empty()){auto pooled=mergeBatchResults(s.input.project,childResults);
            require(numbersEqual(r.at("targets"),pooled.at("targets"))&&numbersEqual(r.at("score_moments"),pooled.at("score_moments"))&&r.at("histories")==pooled.at("histories"),"root scores disagree with complete child moments");
            for(const char* key:{"voxel_field","edep_spectra"})require(numbersEqual(r.value(key,Json(nullptr)),pooled.value(key,Json(nullptr))),"root voxel/spectrum differs from complete child data");}
        else require(r.at("histories")==0&&r.at("targets").empty(),"statistics exist without complete batches");
        auto c=batchConvergence(r,s.options,int(childResults.size()));if(r.at("status")!="completed")c["converged"]=false;
        require(c==r.at("batch").at("convergence"),"recorded convergence differs from actual scores/options");
    }
    if(!s.workerSha.empty())require(shaValid(s.workerSha),"invalid Worker SHA");
    if(r.at("histories")!=0)validateVoxelResult(s.input.project,r);
    qint64 total=0;for(const auto& file:s.files){total+=file.second.size();require(total<=totalLimit,"report evidence exceeds 256 MiB");}
    return s;
}
void validateGeometry(const Project& p,const Json& r){
    if(r.at("histories")==0)return;
    auto cpu=geometrySnapshot(p);const auto& actual=r.at("geometry_audit").at("volumes");
    require(actual.is_array()&&actual.size()==cpu.at("volumes").size(),"geometry audit volume count differs");
    std::map<std::string,Json> map;for(const auto& row:actual)require(map.emplace(row.at("id").get<std::string>(),row).second,"duplicate audited volume ID");
    std::set<std::string> sensitive;
    for(const auto& row:cpu.at("volumes")){auto id=row.at("id").get<std::string>();require(map.count(id),"geometry audit ID missing");const auto& a=map.at(id);
        require(row.at("material")==a.at("material")&&row.at("sensitive")==a.at("sensitive"),"geometry material/target binding differs");
        require(numbersEqual(row.at("world_center_mm"),a.at("world_center_mm"))&&numbersEqual(row.at("world_rotation"),a.at("world_rotation")),"geometry transform differs");
        require(placementEqual(row.at("world_center_mm"),a.at("geant4_world_center_mm"))&&placementEqual(row.at("world_rotation"),a.at("geant4_world_rotation")),"actual Geant4 placement differs from model");
        double mass=a.at("mass_kg"),volume=a.at("geant4_material_volume_mm3"),density=a.at("density_kg_m3");
        require(mass>0&&density>0&&near(volume,row.at("material_volume_mm3").get<double>())&&near(mass,volume*density/1e9),"actual geometry volume/mass is inconsistent");
        if(row.at("sensitive")==true)sensitive.insert(id);
    }
    std::set<std::string> ids;for(const auto& t:r.at("targets")){auto id=t.at("id").get<std::string>();
        require(ids.insert(id).second&&sensitive.count(id)&&near(t.at("mass_kg").get<double>(),map.at(id).at("mass_kg").get<double>()),"target mass/ID differs from actual geometry");}
    require(ids==sensitive,"sensitive volume/result target sets differ");
}
const char* replayScript=R"PS(param([Parameter(Mandatory=$true)][string]$Worker,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$replay = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'replay.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($replay.worker_sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Missing Worker fingerprint.' }
$stream = [IO.File]::OpenRead($Worker)
$algorithm = [Security.Cryptography.SHA256]::Create()
try { $actualSha = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
finally { $stream.Dispose(); $algorithm.Dispose() }
if ($actualSha -ne $replay.worker_sha256) { throw 'Worker SHA mismatch.' }
$runtime = $replay.runtime_audit
$env:PATH = (Split-Path -Parent $Worker) + ';' + (Join-Path $runtime.build_configuration.paths.qt_prefix 'bin') + ';' + $env:PATH
foreach ($entry in $runtime.dataset_environment.PSObject.Properties) {
    if ($entry.Name -notmatch '^G4[A-Z0-9_]+DATA$') { throw 'Unexpected dataset variable.' }
    [Environment]::SetEnvironmentVariable($entry.Name, [string]$entry.Value, 'Process')
}
& $Worker --replay-report $PSScriptRoot $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Replay failed; inspect the output and comparison report.' }
)PS";
}
Json compareReportReplay(const Json& expected,const Json& actual){
    Json checks=Json::array();for(const char* key:{"histories","targets","score_moments"})checks.push_back({{"field",key},{"passed",numbersEqual(expected.at(key),actual.at(key))}});
    for(const char* key:{"voxel_field","edep_spectra"})if(expected.contains(key)||actual.contains(key))checks.push_back({{"field",key},{"passed",numbersEqual(expected.value(key,Json(nullptr)),actual.value(key,Json(nullptr)))}});
    auto a=expected.at("track_segments"),b=actual.at("track_segments");for(auto* list:{&a,&b})for(auto& segment:*list){segment.erase("batch_index");segment.erase("global_event");}
    checks.push_back({{"field","track_segments_local"},{"passed",a==b}});bool passed=true;for(const auto& c:checks)passed=passed&&c.at("passed").get<bool>();
    return {{"schema","scdat.radiation-report-replay-comparison/1"},{"passed",passed},{"checks",checks},
        {"floating_relative_tolerance",1e-12},{"scope","Same Worker/configuration and recorded completed histories; no external physics validation"}};
}
Json exportReproReport(const QString& requested,const QString& output){
    auto target=QFileInfo(output).absoluteFilePath();require(!QFileInfo::exists(target)&&QFileInfo(QFileInfo(target).absolutePath()).isDir(),"output must be a new directory under an existing parent");
    auto s=loadSource(requested);const auto& p=s.input.project;const auto& r=s.result;validateGeometry(p,r);
    Json materials=Json::object();auto geometry=geometrySnapshot(p);for(const auto& v:geometry.at("volumes"))materials[v.at("id").get<std::string>()]=v.at("material");
    Json targets=r.at("targets"),zeros=Json::array(),unconverged=Json::array();for(auto& t:targets){t["material"]=materials.at(t.at("id").get<std::string>());if(t.at("nonzero_histories")==0)zeros.push_back(t.at("id"));}
    std::string state="not_configured";Json criterion=nullptr,convergence=nullptr;
    if(s.archive){criterion=s.options.toJson();convergence=r.at("batch").at("convergence");state=convergence.at("converged")==true?"converged":"unconverged";
        for(const auto& t:convergence.at("targets"))if(t.at("converged")==false)unconverged.push_back(t.at("id"));}
    Json worker={{"sha256",s.workerSha.empty()?Json(nullptr):Json(s.workerSha)},{"path",nullptr}};
    if(r.contains("worker")){worker=r.at("worker");require(worker.at("sha256")==s.workerSha,"result Worker differs from archive identity");}
    else if(r.at("runtime_audit").contains("build_configuration"))worker["path"]=r.at("runtime_audit").at("build_configuration").at("paths").at("worker_directory").get<std::string>()+"/scdat_radiation_worker.exe";
    bool eligible=!s.indices.empty()&&shaValid(s.workerSha);
    Json report={{"schema","scdat.radiation-repro-report/1"},{"generated_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()},
        {"summary",{{"name",p.name},{"job_status",r.at("status")},{"kind",s.archive?"batch":"serial"},{"histories",r.at("histories")},{"requested_histories",p.events},{"synthetic",p.synthetic},{"statistical_state",state}}},
        {"source",{{"job_directory",s.directory.toStdString()},{"project_identity",sourceFileIdentity("project.json",s.files.at("project.json"))},
            {"result_identity",sourceFileIdentity("result.json",s.files.at("result.json"))},{"archive_identity",s.archiveIdentity},{"project_provenance",p.provenance}}},
        {"environment",{{"quantity",quantityName(p.quantity)},{"unit",quantityUnit(p.quantity)},{"duration_s",p.duration},{"source_radius_mm",p.radius},
            {"expected_primaries",p.expectedPrimaries()},{"source_label",p.environmentSource},{"synthetic",p.synthetic},{"metadata",p.environmentMetadata},
            {"missing_metadata",p.missingEnvironmentMetadata()},{"normalization",r.value("normalization",Json(nullptr))}}},
        {"geometry",{{"cpu_contract",geometry},{"actual_audit",r.value("geometry_audit",Json(nullptr))},{"status",r.at("histories")==0?"model_validated_no_complete_transport":"model_validated_actual_audit"}}},
        {"materials",{{"input_snapshot_pinned",!p.materialSnapshot.is_null()},{"audit",r.value("material_audit",Json(nullptr))}}},
        {"configuration",{{"project_schema",p.toJson().at("schema")},{"requested",r.at("requested_configuration")},{"effective",r.at("effective_configuration")},
            {"runtime_audit",r.at("runtime_audit")},{"worker",worker},{"engine",r.at("engine")},{"engine_version",r.at("engine_version")}}},
        {"statistics",{{"criterion",criterion},{"convergence",convergence},{"zero_hit_targets",zeros},{"unconverged_targets",unconverged},
            {"caution","仅含 MC 统计误差；零命中不代表零风险；未验证自适应停止后的置信覆盖率。"}}},
        {"targets",targets},{"batch",s.archive?r.at("batch"):Json(nullptr)},
        {"limitations",r.value("limitations",Json::array())},
        {"replay",{{"eligible",eligible},{"worker_sha256",worker.at("sha256")},{"indices",s.indices},{"kind",s.archive?"selected_completed_batches":"serial_as_one_batch"},
            {"command",".\\reproduce.ps1 -Worker <matching-worker.exe> -OutputDirectory <NEW_DIRECTORY>"}}}};
    if(r.contains("voxel_field")){const auto& f=r.at("voxel_field");Json quality={{"zero_hit",0},{"insufficient",0},{"sufficient",0}};
        for(const auto& row:f.at("rows")){auto key=row.at("quality").get<std::string>();quality[key]=quality.at(key).get<int>()+1;}
        report["voxel_summary"]={{"volume_id",f.at("volume_id")},{"counts",f.at("counts")},{"histories",f.at("histories")},{"quality_counts",quality},
            {"hotspot",f.at("hotspot")},{"geometry_audit",f.at("geometry_audit")},{"caution",f.at("caution")}};
        report["edep_spectra"]=r.at("edep_spectra");
    }
    report["limitations"].push_back("外部物理验证未完成；真实轨道环境、整星 CAD 等价、器件 DDD/SEE 与自动环境模型执行不由此报告认证。");
    report["limitations"].push_back("报告是文件一致性和模型/统计审核，不是数字签名；运行审查不校验所有数据集和 DLL 的完整内容。");
    if(!eligible)report["limitations"].push_back("没有完整历史或缺少 Worker 指纹；报告可阅览，但不允许重跑。");
    Json replay={{"schema","scdat.radiation-report-replay/1"},{"worker_sha256",worker.at("sha256")},{"runtime_audit",r.at("runtime_audit")},
        {"options",s.options.toJson()},{"indices",s.indices},{"expected_histories",r.at("histories")}};
    QTemporaryDir staging(QFileInfo(target).absolutePath()+"/.radiation-report-XXXXXX");require(staging.isValid(),"cannot create report staging directory");
    Json files=Json::array();qint64 total=0;auto add=[&](const QString& name,const QByteArray& bytes){total+=bytes.size();require(total<=totalLimit,"package exceeds 256 MiB");
        writeNew(staging.path()+"/"+name,bytes);files.push_back({{"path",name.toStdString()},{"size_bytes",bytes.size()},{"sha256",archiveSha(bytes)}});};
    for(const auto& f:s.files)add("evidence/"+f.first,f.second);
    add("report.json",jsonBytes(report));add("report.html",renderReproHtml(report).toUtf8());add("replay.json",jsonBytes(replay));add("reproduce.ps1",QByteArray(replayScript));
    writeNew(staging.path()+"/package-manifest.json",jsonBytes({{"schema","scdat.radiation-report-package/1"},{"files",files}}));
    // Input/result snapshots are immutable across this export, even for serial
    // jobs which predate cooperative archive locking.
    require(archiveSha(inside(s.directory,"project.json"))==archiveSha(s.files.at("project.json"))&&archiveSha(inside(s.directory,"result.json"))==archiveSha(s.files.at("result.json")),"source changed during export");
    require(!QFileInfo::exists(target)&&QDir().rename(staging.path(),target),"cannot atomically publish new report directory");staging.setAutoRemove(false);return report;
}
Json verifyReproReport(const QString& request){
    auto root=QFileInfo(request).canonicalFilePath();require(QFileInfo(root).isDir(),"report directory unavailable");auto manifest=Json::parse(inside(root,"package-manifest.json").toStdString());
    require(manifest.at("schema")=="scdat.radiation-report-package/1"&&manifest.at("files").is_array()&&manifest.at("files").size()<=30010,"unsupported package manifest");
    std::set<std::string> paths;qint64 total=0;for(const auto& file:manifest.at("files")){auto name=file.at("path").get<std::string>();require(paths.insert(name).second&&name!="package-manifest.json","duplicate/self-referential package file");
        auto bytes=inside(root,QString::fromStdString(name));total+=bytes.size();require(total<=totalLimit&&file.at("size_bytes")==bytes.size()&&file.at("sha256")==archiveSha(bytes),"package file identity mismatch");}
    for(const char* required:{"report.json","report.html","replay.json","reproduce.ps1","evidence/project.json","evidence/result.json"})require(paths.count(required),"required package file missing");
    auto data=Json::parse(inside(root,"report.json").toStdString());require(data.at("schema")=="scdat.radiation-repro-report/1","unsupported report schema");
    require(data.at("source").at("project_identity").at("sha256")==archiveSha(inside(root,"evidence/project.json"))&&data.at("source").at("result_identity").at("sha256")==archiveSha(inside(root,"evidence/result.json")),"report evidence identity differs");
    return data;
}
ReportReplayInput loadReportReplay(const QString& directory){
    ReportReplayInput a;a.report=verifyReproReport(directory);require(a.report.at("replay").at("eligible")==true,"report is not eligible for replay");
    auto root=QFileInfo(directory).canonicalFilePath();auto replay=Json::parse(inside(root,"replay.json").toStdString());require(replay.at("schema")=="scdat.radiation-report-replay/1","unsupported replay schema");
    a.input=loadProjectFile(root+"/evidence/project.json");a.expected=Json::parse(inside(root,"evidence/result.json").toStdString());
    a.workerSha=replay.at("worker_sha256").get<std::string>();a.runtime=replay.at("runtime_audit");a.options=BatchOptions::fromJson(replay.at("options"));
    require(shaValid(a.workerSha)&&a.report.at("replay").at("worker_sha256")==a.workerSha&&a.expected.at("runtime_audit")==a.runtime,"replay identity/runtime differs");
    auto plan=planBatches(a.input.project,a.options);int previous=-1;std::int64_t histories=0;
    require(replay.at("indices").is_array()&&!replay.at("indices").empty(),"no completed batches to replay");
    for(const auto& value:replay.at("indices")){require(value.is_number_integer(),"replay index must be integer");auto i=value.get<std::int64_t>();
        require(i>previous&&i>=0&&i<std::int64_t(plan.size()),"invalid replay index range/order");a.indices.push_back(int(i));histories+=plan[std::size_t(i)].histories;previous=int(i);}
    require(a.report.at("replay").at("indices")==replay.at("indices")&&histories==replay.at("expected_histories")&&histories==a.expected.at("histories"),"replay count differs from source");
    return a;
}
}
