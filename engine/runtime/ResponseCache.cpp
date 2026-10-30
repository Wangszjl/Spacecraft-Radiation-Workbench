#include "ResponseCache.h"
#include "BatchArchive.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <set>
#include <map>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Response cache: ")+m);}
QByteArray inside(const QString& root,const QString& rel){require(!rel.isEmpty()&&!rel.contains('\\')&&!QDir::isAbsolutePath(rel)&&QDir::cleanPath(rel)==rel&&!rel.startsWith("../"),"unsafe cache path");auto p=QFileInfo(root+"/"+rel).canonicalFilePath();
#ifdef Q_OS_WIN
    require(p.compare(root+"/"+rel,Qt::CaseInsensitive)==0,"redirected cache artifact");
#else
    require(p==root+"/"+rel,"redirected cache artifact");
#endif
    return responseRead(p);
}
Json physical(Json j){j.erase("provenance");return j;}
void saveNewResult(const QString& output,const Json& result){QTemporaryFile file(output+".tmp-XXXXXX");require(file.open(),"cannot create temporary weighted result");auto b=QByteArray::fromStdString(result.dump(2));require(b.size()<=32*1024*1024&&file.write(b)==b.size()&&file.flush(),"cannot save bounded weighted result");file.close();require(file.rename(output),"cannot publish new weighted result; existing output is refused");file.setAutoRemove(false);}
void validateChildAudit(const Json& child,const Json& identity){
    require(child.at("worker").at("sha256")==identity.at("worker_sha256"),"child Worker fingerprint differs from cache");
    std::map<std::string,Json> masses;for(const auto& v:child.at("geometry_audit").at("volumes"))if(v.at("sensitive")==true){auto id=v.at("id").get<std::string>();require(masses.emplace(id,v.at("mass_kg")).second,"duplicate sensitive audit ID");}
    std::set<std::string> ids;for(const auto& t:child.at("targets")){auto id=t.at("id").get<std::string>();require(ids.insert(id).second&&masses.count(id)&&masses.at(id)==t.at("mass_kg"),"target mass/set differs from transport geometry audit");}require(ids.size()==masses.size(),"sensitive geometry target missing");
}
}
QString responseCasePath(int i){return QString("cases/case-%1").arg(i,6,10,QChar('0'));}
QByteArray responseRead(const QString& p){QFile f(p);require(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=32*1024*1024,"missing/empty/oversized cache artifact");auto b=f.readAll();require(f.error()==QFileDevice::NoError,"cannot read cache artifact");return b;}
Json responseFileInventory(const QString& requested){auto root=QFileInfo(requested).canonicalFilePath();require(QFileInfo(root).isDir(),"cache root unavailable");QStringList names;QDirIterator it(root,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);std::int64_t size=0;int count=0;
    while(it.hasNext()){it.next();auto f=it.fileInfo();require(!f.isSymLink(),"cache contains symbolic link");auto rel=QDir(root).relativeFilePath(it.filePath());if(f.isDir()){auto path=f.canonicalFilePath();
#ifdef Q_OS_WIN
        require(path.compare(root+"/"+rel,Qt::CaseInsensitive)==0,"redirected cache directory");
#else
        require(path==root+"/"+rel,"redirected cache directory");
#endif
        continue;}
        if(rel=="manifest.json")continue;require(++count<=50000,"too many cache artifacts");auto b=inside(root,rel);size+=b.size();require(size<=256*1024*1024,"cache exceeds 256 MiB");names.push_back(rel);
    }names.sort();Json files=Json::array();for(const auto& name:names){auto b=inside(root,name);files.push_back({{"path",name.toStdString()},{"size_bytes",b.size()},{"sha256",archiveSha(b)}});}return files;
}
ResponseCache loadResponseCache(const QString& requested){ResponseCache cache;cache.directory=QFileInfo(requested).canonicalFilePath();const auto& root=cache.directory;require(QFileInfo(root).isDir(),"entry unavailable");auto manifest=Json::parse(inside(root,"manifest.json").toStdString());
    require(manifest.is_object()&&manifest.size()==6&&manifest.at("schema")=="scdat.radiation-response-cache-manifest/1"&&manifest.at("status")=="completed","incomplete/unsupported cache manifest");
    const auto& files=manifest.at("files");require(files.is_array()&&files.size()<=50000,"invalid cache manifest files");std::set<std::string> paths;std::int64_t size=0;
    for(const auto& f:files){require(f.is_object()&&f.size()==3,"invalid artifact record");auto name=f.at("path").get<std::string>();require(paths.insert(name).second&&name!="manifest.json","duplicate/recursive cache artifact");auto bytes=inside(root,QString::fromStdString(name));size+=bytes.size();require(size<=256*1024*1024&&f.at("size_bytes")==bytes.size()&&f.at("sha256")==archiveSha(bytes),"cache artifact SHA/size mismatch");}
    require(responseFileInventory(root)==files,"cache inventory differs");for(const char* path:{"project.json","response-options.json","identity.json","matrix.json"})require(paths.count(path),"required cache file missing");
    cache.input=loadProjectFile(root+"/project.json");cache.options=ResponseOptions::fromJson(Json::parse(inside(root,"response-options.json").toStdString()));auto identity=Json::parse(inside(root,"identity.json").toStdString());
    auto expected=responseCacheIdentity(cache.input.project,cache.options,identity.at("worker_sha256").get<std::string>(),identity.at("runtime_audit"));require(identity==expected&&manifest.at("identity")==identity&&manifest.at("cache_key")==archiveSha(QByteArray::fromStdString(identity.dump())),"cache identity/key differs");
    auto plan=planResponseCases(cache.input.project,cache.options);require(manifest.at("case_count")==plan.size(),"case count differs");std::vector<Json> results;
    for(const auto& c:plan){auto rel=responseCasePath(c.index),directory=root+"/"+rel;require(paths.count((rel+"/project.json").toStdString())&&paths.count((rel+"/job/manifest.json").toStdString()),"case archive missing");auto input=loadProjectFile(directory+"/project.json");require(physical(input.project.toJson())==physical(c.project.toJson()),"basis project differs");auto archive=loadBatchArchive(directory+"/job");
        require(archive.manifest.at("status")=="completed"&&archive.manifest.at("worker_sha256")==identity.at("worker_sha256")&&archive.manifest.at("runtime_audit")==identity.at("runtime_audit"),"basis Worker/runtime/status differs");require(physical(archive.input.project.toJson())==physical(input.project.toJson())&&archive.options.toJson()==cache.options.batchOptions().toJson(),"basis archive input/options differ");
        auto batches=planBatches(c.project,cache.options.batchOptions());require(archive.completed.size()==batches.size(),"partial basis batch budget");std::vector<Json> children;for(const auto& child:archive.completed){validateChildAudit(child.result,identity);children.push_back(child.result);}auto merged=mergeBatchResults(archive.input.project,children);auto result=Json::parse(inside(directory+"/job","result.json").toStdString());
        require(result.at("status")=="completed"&&result.at("histories")==cache.options.historiesPerCase&&result.at("targets")==merged.at("targets")&&result.at("score_moments")==merged.at("score_moments")&&result.at("expected_primaries")==merged.at("expected_primaries"),"basis summary differs from child transport");results.push_back(std::move(result));
    }cache.matrix=Json::parse(inside(root,"matrix.json").toStdString());require(cache.matrix==makeResponseMatrix(cache.input.project,cache.options,results),"matrix differs from original transport");cache.manifest=std::move(manifest);return cache;
}
Json applyResponseCache(const LoadedProject& input,const QString& entry,const QString& output,const std::string& workerSha,const Json& runtime){require(!QFileInfo::exists(output),"refusing existing weighted result");auto cache=loadResponseCache(entry);auto identity=responseCacheIdentity(input.project,cache.options,workerSha,runtime);require(identity==cache.manifest.at("identity"),"project, Worker or runtime differs from cache");auto result=applyResponseMatrix(input.project,cache.options,cache.matrix);result["project"]=input.project.toJson();result["project_sha256"]=archiveSha(input.bytes);result["cache_key"]=cache.manifest.at("cache_key");result["cache_directory"]=cache.directory.toStdString();result["cache_manifest_sha256"]=archiveSha(responseRead(cache.directory+"/manifest.json"));result["worker_sha256"]=workerSha;result["runtime_audit"]=runtime;saveNewResult(output,result);return result;}
}
