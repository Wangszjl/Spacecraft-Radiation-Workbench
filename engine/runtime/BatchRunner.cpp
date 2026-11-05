#include "BatchRunner.h"
#include "RadiationRuntime.h"
#include "BatchArchive.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <iostream>
#include <memory>
#include <algorithm>
#include <map>
#include <stdexcept>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Batch runner: ")+m);}
std::string hash(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex().toStdString();}
QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"cannot read batch artifact");return f.readAll();}
void rawFile(const QString& path,const QByteArray& bytes){QFile f(path);require(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size(),"cannot create batch input");}
void writeManifest(const QString& path,const Json& value){
    // Windows readers may briefly deny rename/delete sharing. Retry the entire
    // atomic save with a new temporary file; never fall back to in-place writes.
    for(int attempt=0;;++attempt){try{writeResult(path,value);return;}catch(const std::exception&){if(attempt==19)throw;QThread::msleep(10);}}
}
struct Child {
    BatchEntry entry;QString directory;QProcess process;Json record;bool consumed=false;
    explicit Child(BatchEntry e,QString d):entry(e),directory(d){}
    ~Child(){if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(3000);}}
    void log(){auto bytes=process.readAllStandardOutput();if(bytes.isEmpty())return;
        QFile f(directory+"/transport.log");require(f.open(QIODevice::WriteOnly|QIODevice::Append)&&f.write(bytes)==bytes.size(),"cannot save child log");}
};
// Closing the parent, including forced UI termination, stops its child workers.
struct ChildGroup {
#ifdef Q_OS_WIN
    HANDLE handle=CreateJobObjectW(nullptr,nullptr);
    ChildGroup(){require(handle!=nullptr,"cannot create child job object");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(handle,JobObjectExtendedLimitInformation,&limit,sizeof(limit))){CloseHandle(handle);handle=nullptr;require(false,"cannot configure child job object");}}
    void add(QProcess& p){HANDLE child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(p.processId()));
        bool ok=child&&AssignProcessToJobObject(handle,child);if(child)CloseHandle(child);require(ok,"cannot protect child worker lifetime");}
    ~ChildGroup(){if(handle)CloseHandle(handle);}
#else
    void add(QProcess&){}
#endif
};
}
static Json runBatchJobInternal(const LoadedProject& loaded,const BatchOptions& options,const QString& requested,const Json& runtime,const BatchArchive* source,const std::vector<int>* replay=nullptr,const QString& externalCancel={}){
    if(!loaded.project.sourceSampling.is_null())throw std::runtime_error("Weighted sampling is not supported by the legacy batch runner");
    auto plan=planBatches(loaded.project,options);const auto& project=loaded.project;
    QString directory=QFileInfo(requested).absoluteFilePath();require(!QFileInfo::exists(directory),"output directory already exists");
    QElapsedTimer timer;timer.start();auto started=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    require(QDir().mkpath(directory),"cannot create batch directory");
    QLockFile jobLock(directory+"/job.lock");jobLock.setStaleLockTime(0);require(jobLock.tryLock(0),"cannot lock new batch directory");
    rawFile(directory+"/project.json",loaded.bytes);writeResult(directory+"/options.json",options.toJson());
    auto planJson=batchPlanJson(plan);writeResult(directory+"/plan.json",planJson);
    const auto workerSha=archiveSha(read(QCoreApplication::applicationFilePath()));
    Json manifestBase={{"schema","scdat.radiation-batch-manifest/2"},{"worker_sha256",workerSha},
        {"project_sha256",archiveSha(loaded.bytes)},{"options_sha256",archiveSha(read(directory+"/options.json"))},
        {"plan_sha256",archiveSha(read(directory+"/plan.json"))},{"runtime_audit",runtime},{"plan",planJson}};
    std::map<int,Json> recordMap;auto records=[&]{Json list=Json::array();for(const auto& pair:recordMap)list.push_back(pair.second);return list;};
    std::vector<Json> complete;std::vector<int> completedIndices;Json reusedIndices=Json::array();int reusedHistories=0;
    if(source)for(const auto& previous:source->completed){
        int index=previous.record.at("index");QString relative=QString("batch-%1").arg(index,6,10,QChar('0'));
        require(QDir().mkpath(directory+"/"+relative),"cannot create reused batch directory");
        rawFile(directory+"/"+relative+"/project.json",previous.projectBytes);rawFile(directory+"/"+relative+"/result.json",previous.resultBytes);
        rawFile(directory+"/"+relative+"/transport.log",previous.logBytes);
        auto record=previous.record;record["reused"]=true;record["reused_from"]=source->identity;recordMap[index]=record;
        reusedIndices.push_back(index);reusedHistories+=record.at("histories").get<int>();
    }
    auto readCompleted=[&]{complete.clear();completedIndices.clear();for(const auto& pair:recordMap)if(pair.second.at("status")=="completed"){
        complete.push_back(Json::parse(read(directory+"/"+QString::fromStdString(pair.second.at("directory"))+"/result.json").toStdString()));completedIndices.push_back(pair.first);}};
    std::string reason="history_budget_exhausted",status="completed",error;
    if(replay)reason="replay_completed";
    auto timedOut=[&]{return options.maxSeconds>0&&timer.elapsed()/1000.0>=options.maxSeconds;};
    auto cancelled=[&]{return QFileInfo::exists(directory+"/cancel.request")||(!externalCancel.isEmpty()&&QFileInfo::exists(externalCancel));};
    auto checkpoint=[&]{auto manifest=manifestBase;manifest["status"]="running";manifest["batches"]=records();manifest["elapsed_seconds"]=timer.elapsed()/1000.0;writeManifest(directory+"/manifest.json",manifest);};
    checkpoint();readCompleted();std::vector<BatchEntry> pending;
    for(const auto& entry:plan)if(!recordMap.count(entry.index)&&(!replay||std::find(replay->begin(),replay->end(),entry.index)!=replay->end()))pending.push_back(entry);
    if(timedOut()){reason="time_budget_exhausted";pending.clear();}
    else if(!replay&&!complete.empty()&&batchConvergence(mergeBatchResults(project,complete),options,int(complete.size())).at("converged")==true){reason="target_reached";pending.clear();}
    ChildGroup group;
    for(std::size_t first=0;first<pending.size();first+=options.maxParallel){
        if(cancelled()){reason="cancelled";status="cancelled";break;}if(timedOut()){reason="time_budget_exhausted";break;}
        std::vector<std::unique_ptr<Child>> children;
        try{
            for(std::size_t k=first;k<std::min(pending.size(),first+std::size_t(options.maxParallel));++k){
                if(cancelled()||timedOut())break;
                int i=pending[k].index;
                QString relative=QString("batch-%1").arg(int(i),6,10,QChar('0'));auto child=std::make_unique<Child>(plan[i],directory+"/"+relative);
                require(QDir().mkpath(child->directory),"cannot create child directory");
                auto p=project;p.events=plan[i].histories;p.seed=plan[i].seed;saveProjectFile(child->directory+"/project.json",p);
                child->record=planJson[i];child->record["directory"]=relative.toStdString();child->record["project_sha256"]=hash(read(child->directory+"/project.json"));
                child->record["status"]="running";child->record["reused"]=false;child->record["started_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                child->process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
                child->process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a){a->flags|=CREATE_NO_WINDOW;});
#endif
                child->process.start(QCoreApplication::applicationFilePath(),{child->directory+"/project.json",child->directory+"/result.json"});
                require(child->process.waitForStarted(3000),"child worker failed to start");group.add(child->process);
                child->record["process_id"]=child->process.processId();recordMap[i]=child->record;children.push_back(std::move(child));
            }
            checkpoint();bool stopping=false;
            for(;;){
                bool any=false,changed=false;
                for(auto& c:children){
                    if(c->consumed)continue;c->process.waitForFinished(1);c->log();
                    if(c->process.state()!=QProcess::NotRunning){any=true;continue;}
                    c->consumed=true;c->record["finished_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                    if(c->process.exitStatus()==QProcess::NormalExit&&c->process.exitCode()==0){
                        auto bytes=read(c->directory+"/result.json");auto result=Json::parse(bytes.toStdString());
                        require(result.at("project_sha256")==c->record.at("project_sha256"),"child input SHA mismatch");
                        require(result.at("histories")==c->entry.histories&&result.at("project").at("seed")==c->entry.seed,"child plan differs");
                        require(result.at("runtime_audit")==runtime,"child runtime differs from parent audit");
                        c->record["status"]="completed";c->record["result_sha256"]=hash(bytes);
                        c->record["transport_log_sha256"]=hash(read(c->directory+"/transport.log"));
                    }else if(stopping){c->record["status"]="discarded";}
                    else{c->record["status"]="failed";status="failed";reason="child_failed";error="Child worker exit failure";stopping=true;}
                    recordMap[c->entry.index]=c->record;changed=true;
                }
                if(changed)checkpoint();
                if(cancelled()){reason="cancelled";status="cancelled";stopping=true;}
                else if(timedOut()&&!stopping){reason="time_budget_exhausted";stopping=true;}
                if(stopping)for(auto& c:children)if(c->process.state()!=QProcess::NotRunning)c->process.kill();
                if(!any)break;QThread::msleep(5);
            }
            // Merge in plan order, regardless of completion order.
            readCompleted();
            checkpoint();
            if(!complete.empty()){
                auto merged=mergeBatchResults(project,complete);
                std::cout<<"SCDAT_PROGRESS "<<merged.at("histories")<<" "<<project.events<<std::endl;
                if(!replay&&!stopping&&batchConvergence(merged,options,int(complete.size())).at("converged")==true){reason="target_reached";break;}
            }
            if(stopping)break;
        }catch(const std::exception& e){
            status="failed";reason="child_failed";error=e.what();
            for(auto& c:children)if(c->record.at("status")=="running"){
                if(c->process.state()!=QProcess::NotRunning){c->process.kill();c->process.waitForFinished(3000);}
                c->log();c->record["status"]="discarded";c->record["finished_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                recordMap[c->entry.index]=c->record;
            }break;
        }
    }
    // Include every fully verified child even if a sibling failed later in a wave.
    readCompleted();
    Json merged={{"histories",0},{"targets",Json::array()},{"track_segments",Json::array()},{"score_moments",Json::object()},
        {"expected_primaries",project.expectedPrimaries()}};
    try{if(!complete.empty())merged=mergeBatchResults(project,complete);}
    catch(const std::exception& e){status="failed";reason="merge_failed";error=e.what();}
    if(!completedIndices.empty())for(auto& segment:merged["track_segments"]){segment["batch_index"]=completedIndices.front();
        segment["global_event"]=plan[completedIndices.front()].firstEvent+segment.at("event").get<int>();}
    auto convergence=batchConvergence(merged,options,int(complete.size()));if(status!="completed")convergence["converged"]=false;
    merged=addProvenance(std::move(merged),project,loaded.bytes,started,runtime);merged["status"]=status;merged["input_schema"]=loaded.inputSchema;
    merged["effective_configuration"]={{"histories",merged.at("histories")},{"threading","independent serial child processes"},
        {"seed_policy","base seed + batch index modulo 2147483646; no seed reuse within this plan"},
        {"max_parallel",options.maxParallel},{"time_multiplier_applied",project.quantity==SpectrumQuantity::Flux}};
    merged["batch"]={{"schema","scdat.radiation-batch-result/1"},{"options",options.toJson()},{"plan",planJson},
        {"batches",records()},{"completed_batches",complete.size()},{"stop_reason",reason},{"elapsed_seconds",timer.elapsed()/1000.0},
        {"convergence",convergence},{"directory",directory.toStdString()},{"error",error},
        {"merge_rule","Chan pooled event mean/M2; exposure applied once; all zero histories included"},
        {"reproducibility","Fixed plan merges in index order; stopping at wave boundaries depends on max_parallel; time budgets depend on wall clock"},
        {"random_stream_scope","Distinct seeds and disjoint logical event IDs; no mathematical proof of PRNG stream disjointness"}};
    merged["batch"]["worker_sha256"]=workerSha;
    if(replay)merged["batch"]["replay"]={{"selected_indices",*replay},{"policy","Recompute every selected batch; no reuse, time budget or adaptive early stop"}};
    if(source)merged["batch"]["resume"]={{"source",source->identity},{"reused_indices",reusedIndices},
        {"reused_batches",reusedIndices.size()},{"reused_histories",reusedHistories},{"policy","Complete verified batches copied unchanged; pending indices only; new wall-clock budget"}};
    merged["limitations"][0]="Forward MC with independent serial processes; boxes, hollow boxes, solid cylinders and spheres; no curved shells or CAD transport";
    writeResult(directory+"/result.json",merged);
    auto manifest=manifestBase;manifest["status"]=status;manifest["stop_reason"]=reason;manifest["batches"]=records();
    manifest["elapsed_seconds"]=timer.elapsed()/1000.0;manifest["result_sha256"]=hash(read(directory+"/result.json"));writeManifest(directory+"/manifest.json",manifest);
    return merged;
}
Json runBatchJob(const LoadedProject& loaded,const BatchOptions& options,const QString& directory,const Json& runtime,const QString& externalCancel){return runBatchJobInternal(loaded,options,directory,runtime,nullptr,nullptr,externalCancel);}
Json resumeBatchJob(const QString& source,const BatchOptions& options,const QString& directory,const Json& runtime){
    auto archive=loadBatchArchive(source);auto input=prepareBatchResume(archive,options,runtime,archiveSha(read(QCoreApplication::applicationFilePath())));
    return runBatchJobInternal(input,options,directory,runtime,&archive);
}
Json replayBatchJob(const LoadedProject& input,const BatchOptions& raw,const std::vector<int>& indices,const QString& directory,const Json& runtime){
    auto options=raw;options.maxSeconds=0;options.maxParallel=1;auto plan=planBatches(input.project,options);
    require(!indices.empty(),"report contains no complete histories to replay");int previous=-1;
    for(int i:indices){require(i>previous&&i>=0&&i<int(plan.size()),"invalid report replay indices");previous=i;}
    return runBatchJobInternal(input,options,directory,runtime,nullptr,&indices);
}
}
