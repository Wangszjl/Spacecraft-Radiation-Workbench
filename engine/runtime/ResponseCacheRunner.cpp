#include "ResponseCacheRunner.h"
#include "BatchRunner.h"
#include "BatchArchive.h"
#include "RadiationRuntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QUuid>
#include <iostream>
namespace spacerad::engine {
namespace {void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Response build: ")+m);}void raw(const QString& p,const QByteArray& bytes){QFile f(p);require(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size(),"cannot save new artifact");}}
Json runResponseCache(const LoadedProject& input,const ResponseOptions& rawOptions,const QString& requestedRoot,const QString& newJob,const Json& runtime){auto options=ResponseOptions::fromJson(rawOptions.toJson());auto plan=planResponseCases(input.project,options);auto workerSha=archiveSha(responseRead(QCoreApplication::applicationFilePath()));auto identity=responseCacheIdentity(input.project,options,workerSha,runtime);auto key=archiveSha(QByteArray::fromStdString(identity.dump()));
    auto root=QFileInfo(requestedRoot).canonicalFilePath(),job=QFileInfo(newJob).absoluteFilePath();require(QFileInfo(root).isDir(),"cache root must already exist");require(!QFileInfo::exists(job)&&QFileInfo(QFileInfo(job).absolutePath()).isDir(),"job must be new under an existing parent");require(QDir().mkpath(job),"cannot create job");QLockFile jobLock(job+"/job.lock");jobLock.setStaleLockTime(0);require(jobLock.tryLock(0),"job locked");
    raw(job+"/project.json",input.bytes);writeResult(job+"/response-options.json",options.toJson());auto entry=root+"/"+QString::fromStdString(key);Json result={{"schema","scdat.radiation-response-build/1"},{"status","running"},{"cache_hit",false},{"cache_key",key},{"cache_directory",entry.toStdString()},{"completed_cases",0},{"case_count",plan.size()},{"error",""}};writeResult(job+"/cache-result.json",result);
    try{QLockFile lock(root+"/"+QString::fromStdString(key)+".lock");lock.setStaleLockTime(0);require(lock.tryLock(0),"cache key already being built");
        if(QFileInfo::exists(entry)){auto cache=loadResponseCache(entry);require(cache.manifest.at("identity")==identity,"existing entry identity differs");result["cache_hit"]=true;result["completed_cases"]=plan.size();result["status"]="completed";}
        else{auto staging=root+"/.building-"+QUuid::createUuid().toString(QUuid::Id128);require(QDir().mkpath(staging),"cannot create staging");result["staging_directory"]=staging.toStdString();writeResult(job+"/cache-result.json",result);raw(staging+"/project.json",input.bytes);writeResult(staging+"/response-options.json",options.toJson());writeResult(staging+"/identity.json",identity);std::vector<Json> results;
            for(const auto& c:plan){if(QFileInfo::exists(job+"/cancel.request")){result["status"]="cancelled";break;}auto dir=staging+"/"+responseCasePath(c.index);require(QDir().mkpath(dir),"cannot create case");saveProjectFile(dir+"/project.json",c.project);auto value=runBatchJob(loadProjectFile(dir+"/project.json"),options.batchOptions(),dir+"/job",runtime,job+"/cancel.request");
                if(value.at("status")!="completed"){result["status"]=value.at("status");result["error"]=value.at("batch").at("error");break;}require(value.at("histories")==options.historiesPerCase,"basis did not consume its fixed budget");results.push_back(std::move(value));result["completed_cases"]=results.size();writeResult(job+"/cache-result.json",result);std::cout<<"SCDAT_RESPONSE_PROGRESS "<<results.size()<<" "<<plan.size()<<std::endl;}
            if(results.size()==plan.size()){require(!QFileInfo::exists(job+"/cancel.request"),"cancelled before publication");writeResult(staging+"/matrix.json",makeResponseMatrix(input.project,options,results));Json manifest={{"schema","scdat.radiation-response-cache-manifest/1"},{"status","completed"},{"cache_key",key},{"identity",identity},{"case_count",plan.size()},{"files",responseFileInventory(staging)}};writeResult(staging+"/manifest.json",manifest);loadResponseCache(staging);
                require(!QFileInfo::exists(job+"/cancel.request"),"cancelled before publication");require(QDir().rename(staging,entry),"atomic cache publication failed");result["status"]="completed";
            }else if(result.at("status")=="running")result["status"]="failed";
        }
    }catch(const std::exception& e){result["status"]=QFileInfo::exists(job+"/cancel.request")?"cancelled":"failed";result["error"]=e.what();}
    writeResult(job+"/cache-result.json",result);return result;
}
}
