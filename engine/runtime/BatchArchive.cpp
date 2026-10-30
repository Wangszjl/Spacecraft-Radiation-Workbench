#include "BatchArchive.h"
#include "VoxelStatistics.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool v,const char* message){if(!v)throw std::runtime_error(std::string("Batch archive: ")+message);}
constexpr qint64 fileLimit=32*1024*1024,totalLimit=256*1024*1024;
QByteArray bounded(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=fileLimit,"missing, empty or oversized archive file (32 MiB limit)");
    auto b=f.read(fileLimit+1);require(f.error()==QFileDevice::NoError&&!b.isEmpty()&&b.size()<=fileLimit,"cannot read bounded archive file");return b;}
QByteArray contained(const QString& root,const QString& relative){
    const auto absolute=QDir::cleanPath(root+"/"+relative);const auto actual=QFileInfo(absolute).canonicalFilePath();
#ifdef Q_OS_WIN
    require(actual.compare(absolute,Qt::CaseInsensitive)==0,"archive path escapes or uses a redirected file");
#else
    require(actual==absolute,"archive path escapes or uses a redirected file");
#endif
    return bounded(absolute);
}
int indexOf(const Json& v,int maximum){require(v.is_number_integer(),"batch index must be integer");auto i=v.get<std::int64_t>();require(i>=0&&i<maximum,"batch index out of range");return int(i);}
Json computational(Json p){for(const char* k:{"events","seed","provenance","name"})p.erase(k);return p;}
}
std::string archiveSha(const QByteArray& bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();}
BatchArchive loadBatchArchive(const QString& requested){
    BatchArchive a;a.directory=QFileInfo(requested).canonicalFilePath();require(QFileInfo(a.directory).isDir(),"archive directory unavailable");
    a.lock=std::make_unique<QLockFile>(a.directory+"/job.lock");a.lock->setStaleLockTime(0);
    require(a.lock->tryLock(0),"archive is active or locked; wait for the source job to exit");
    auto manifestBytes=contained(a.directory,"manifest.json");a.manifest=Json::parse(manifestBytes.toStdString());
    require(a.manifest.at("schema")=="scdat.radiation-batch-manifest/2","only manifest/2 archives are resumable; older archives lack Worker/file fingerprints");
    require(a.manifest.at("status")=="running"||a.manifest.at("status")=="completed"||a.manifest.at("status")=="cancelled"||a.manifest.at("status")=="failed","unsupported manifest status");
    auto projectBytes=contained(a.directory,"project.json"),optionsBytes=contained(a.directory,"options.json"),planBytes=contained(a.directory,"plan.json");
    require(archiveSha(projectBytes)==a.manifest.at("project_sha256")&&archiveSha(optionsBytes)==a.manifest.at("options_sha256")&&archiveSha(planBytes)==a.manifest.at("plan_sha256"),"root input/options/plan SHA mismatch");
    require(a.manifest.at("worker_sha256").is_string()&&a.manifest.at("worker_sha256").get<std::string>().size()==64,"Worker fingerprint missing");
    a.input=loadProjectFile(a.directory+"/project.json");require(a.input.bytes==projectBytes,"archive project changed while reading");
    a.options=BatchOptions::fromJson(Json::parse(optionsBytes.toStdString()));auto plan=planBatches(a.input.project,a.options);auto planned=batchPlanJson(plan);
    require(Json::parse(planBytes.toStdString())==planned&&a.manifest.at("plan")==planned,"archive plan differs from input/options");
    const auto& records=a.manifest.at("batches");require(records.is_array()&&records.size()<=plan.size(),"invalid archive batch records");
    std::set<int> indices;int previous=-1;qint64 bytesRead=projectBytes.size()+optionsBytes.size()+planBytes.size()+manifestBytes.size();std::vector<Json> results;
    for(const auto& record:records){
        int i=indexOf(record.at("index"),int(plan.size()));require(indices.insert(i).second&&i>previous,"duplicate or unsorted batch index");previous=i;
        for(const char* key:{"index","seed","event_begin","event_end_exclusive","histories"})require(record.at(key)==planned[i].at(key),"batch record differs from plan");
        QString relative=QString("batch-%1").arg(i,6,10,QChar('0'));require(record.at("directory")==relative.toStdString(),"invalid batch directory");
        const auto state=record.at("status");require(state=="running"||state=="completed"||state=="failed"||state=="discarded","invalid child status");
        if(state!="completed")continue;
        ArchivedBatch b;b.record=record;b.projectBytes=contained(a.directory,relative+"/project.json");b.resultBytes=contained(a.directory,relative+"/result.json");
        b.logBytes=contained(a.directory,relative+"/transport.log");bytesRead+=b.projectBytes.size()+b.resultBytes.size()+b.logBytes.size();require(bytesRead<=totalLimit,"archive exceeds 256 MiB recovery budget");
        require(archiveSha(b.projectBytes)==record.at("project_sha256")&&archiveSha(b.resultBytes)==record.at("result_sha256")&&archiveSha(b.logBytes)==record.at("transport_log_sha256"),"completed child artifact SHA mismatch");
        auto input=loadProjectFile(a.directory+"/"+relative+"/project.json");require(input.bytes==b.projectBytes,"child input changed while reading");
        b.result=Json::parse(b.resultBytes.toStdString());
        require(b.result.at("project_sha256")==record.at("project_sha256")&&b.result.at("histories")==plan[i].histories&&b.result.at("project").at("seed")==plan[i].seed,"child result identity/plan mismatch");
        require(b.result.at("project")==input.project.toJson(),"child embedded project differs from actual input");
        require(b.result.at("runtime_audit")==a.manifest.at("runtime_audit"),"child runtime differs from archive");
        results.push_back(b.result);a.completed.push_back(std::move(b));
    }
    // Validate all completed event moments, target identities, material/runtime
    // agreement and mission exposure through the ordinary strict pooling path.
    Json pooled;if(!results.empty())pooled=mergeBatchResults(a.input.project,results);
    if(a.manifest.at("status")!="running"){
        auto finalBytes=contained(a.directory,"result.json");require(archiveSha(finalBytes)==a.manifest.at("result_sha256"),"terminal result SHA mismatch");
        auto final=Json::parse(finalBytes.toStdString());
        require(final.at("status")==a.manifest.at("status")&&final.at("project_sha256")==a.manifest.at("project_sha256")&&
            final.at("batch").at("batches")==records&&final.at("batch").at("plan")==planned&&final.at("batch").at("options")==a.options.toJson(),"terminal result and manifest disagree");
        if(!pooled.is_null()&&!a.input.project.voxelScoring.is_null()){
            validateVoxelResult(a.input.project,final);
            require(final.at("voxel_field")==pooled.at("voxel_field")&&final.at("edep_spectra")==pooled.at("edep_spectra"),"terminal voxel/spectrum differs from complete children");}
    }
    a.identity={{"directory",a.directory.toStdString()},{"manifest_sha256",archiveSha(manifestBytes)},{"project_sha256",archiveSha(projectBytes)},
        {"worker_sha256",a.manifest.at("worker_sha256")},{"completed_batches",a.completed.size()},{"status",a.manifest.at("status")}};
    return a;
}
Json batchArchiveReport(const BatchArchive& a,const Json& runtime,const std::string& workerSha){
    std::int64_t histories=0;Json reused=Json::array();for(const auto& b:a.completed){histories+=b.record.at("histories").get<int>();reused.push_back(b.record.at("index"));}
    return {{"schema","scdat.radiation-batch-inspection/1"},{"status","verified"},{"source",a.identity},{"options",a.options.toJson()},
        {"completed_histories",histories},{"reusable_indices",reused},{"planned_batches",a.manifest.at("plan").size()},
        {"resume_compatible",a.manifest.at("worker_sha256")==workerSha&&a.manifest.at("runtime_audit")==runtime},
        {"scope","Complete checkpointed batches only; hashes are integrity checks, not signatures or physical validation"}};
}
LoadedProject prepareBatchResume(const BatchArchive& a,const BatchOptions& options,const Json& runtime,const std::string& workerSha){
    require(a.manifest.at("worker_sha256")==workerSha,"Worker executable differs; create a fresh simulation");
    require(a.manifest.at("runtime_audit")==runtime,"SDK/runtime/dataset configuration differs");
    require(options.historiesPerBatch==a.options.historiesPerBatch&&options.maxHistories>=a.options.maxHistories,"resume cannot change batch size or reduce total budget");
    auto p=a.input.project;p.events=options.maxHistories;auto plan=planBatches(p,options);auto planned=batchPlanJson(plan);Json indices=Json::array();
    for(const auto& b:a.completed){int i=b.record.at("index");require(i<int(plan.size()),"completed index no longer fits");
        for(const char* key:{"index","seed","event_begin","event_end_exclusive","histories"})require(b.record.at(key)==planned[i].at(key),"budget extension changes a completed batch; retain an aligned boundary");
        indices.push_back(i);
    }
    p.provenance["batch_resume"]={{"source",a.identity},{"reused_indices",indices},{"source_options",a.options.toJson()}};
    p.provenance["workbench_batch_options"]=options.toJson();p.validate();auto bytes=QByteArray::fromStdString(p.toJson().dump(2));
    return {p,bytes,p.toJson().at("schema").get<std::string>()};
}
Json compareProjectFiles(const QString& left,const QString& right){
    auto lbytes=bounded(left),rbytes=bounded(right);auto l=loadProjectFile(left),r=loadProjectFile(right);
    require(l.bytes==lbytes&&r.bytes==rbytes,"project changed while comparing");
    auto lj=l.project.toJson(),rj=r.project.toJson();auto lraw=Json::parse(lbytes.toStdString()),rraw=Json::parse(rbytes.toStdString());
    // Ignore only the reader's newly generated identity; compare source metadata
    // as actually supplied in the two files, including all other provenance.
    lj["provenance"]=lraw.value("provenance",Json::object());rj["provenance"]=rraw.value("provenance",Json::object());
    auto patch=Json::diff(lj,rj);require(patch.size()<=1000,"comparison exceeds 1000 differences; compare smaller inputs");
    return {{"schema","scdat.radiation-history-comparison/1"},{"left",sourceFileIdentity(left,lbytes)},{"right",sourceFileIdentity(right,rbytes)},
        {"normalized_inputs_equal",lj==rj},{"transport_inputs_equal",computational(lj)==computational(rj)},
        {"changes",patch},{"change_count",patch.size()},
        {"scope","Normalized project input differences as RFC 6902 operations; no independent-sample significance or physical validation claim"}};
}
}
