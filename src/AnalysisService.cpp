#include "spacerad/AnalysisService.h"
#include "Geometry.h"
#include <spacerad/engine/SourceSampling.h>
#include "runtime/ProjectFile.h"
#include "runtime/VoxelStatistics.h"
#include "runtime/TargetFluence.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
#include <set>
#include <stdexcept>
namespace spacerad {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QByteArray read(const QString& path,qint64 maximum){QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()>=0&&f.size()<=maximum,"Missing or oversized runtime/result file");return f.readAll();}
QByteArray digest(const QByteArray& bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
QByteArray executableHash(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read Worker executable");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&f),"Cannot fingerprint Worker executable");return hash.result().toHex();}
}
struct AnalysisService::Impl {
 RuntimeOptions options;QProcess process;QString state="ready",directory;QByteArray accepted,pending,input,workerHash,contextHash,manifestHash;Json expectedProject,fluence;QByteArray fluenceBytes;int histories=0;bool cancelled=false;QFile log;
};
QProcessEnvironment AnalysisService::childEnvironment(const RuntimeOptions& options){
 require(QFileInfo(options.worker).isAbsolute()&&QFileInfo(options.worker).isFile(),"Configure an absolute existing Worker executable");
 require(QFileInfo(options.dataRoot).isAbsolute()&&QFileInfo(options.dataRoot).isDir(),"Configure an absolute existing Geant4 dataset root");
 auto env=QProcessEnvironment::systemEnvironment();auto manifest=read(QFileInfo(options.worker).dir().filePath("radiation-datasets.txt"),65536);std::set<QByteArray> seen;
 for(auto line:manifest.split('\n')){line=line.trimmed();if(line.isEmpty())continue;auto fields=line.split('|');require(fields.size()==2&&!fields[0].isEmpty()&&!fields[1].isEmpty()&&seen.insert(fields[0]).second,"Malformed dataset manifest");
  auto name=QString::fromUtf8(fields[1]);require(QFileInfo(name).fileName()==name&&name!="."&&name!="..","Manifest dataset must be a directory name");auto path=QDir(options.dataRoot).filePath(name);QDir dir(path);require(dir.exists()&&!dir.entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty(),"Required Geant4 dataset missing or empty");env.insert(QString::fromUtf8(fields[0]),QFileInfo(path).absoluteFilePath());
 }
 for(auto mandatory:{"G4LEDATA","G4PARTICLEXSDATA","G4LEVELGAMMADATA","G4ENSDFSTATEDATA","G4RADIOACTIVEDATA"})require(seen.count(mandatory)>0,"Incomplete dataset manifest");
 QStringList dirs{QFileInfo(options.worker).absolutePath()};for(auto path:options.libraryDirectories){require(QFileInfo(path).isAbsolute()&&QFileInfo(path).isDir(),"Library directories must be absolute existing directories");dirs<<path;}
#ifdef Q_OS_WIN
 const QString key="PATH";
#elif defined(Q_OS_MAC)
 const QString key="DYLD_LIBRARY_PATH";
#else
 const QString key="LD_LIBRARY_PATH";
#endif
 dirs<<env.value(key);env.insert(key,dirs.join(QDir::listSeparator()));return env;
}
AnalysisService::AnalysisService(QObject* parent):QObject(parent),d(new Impl){
 d->process.setProcessChannelMode(QProcess::MergedChannels);
 connect(&d->process,&QProcess::readyReadStandardOutput,this,[this]{
  auto bytes=d->process.readAllStandardOutput();if(d->log.isOpen())d->log.write(bytes);emit logMessage(QString::fromUtf8(bytes));d->pending+=bytes;
  while(d->pending.contains('\n')){int end=d->pending.indexOf('\n');auto line=d->pending.left(end).trimmed();d->pending.remove(0,end+1);auto m=QRegularExpression("^SCDAT_PROGRESS ([0-9]+) ([0-9]+)$").match(QString::fromUtf8(line));if(m.hasMatch()){int n=m.captured(1).toInt(),total=m.captured(2).toInt();if(total==d->histories&&n>=0&&n<=total)emit progress(n,total);}}
  if(d->pending.size()>1024*1024)d->pending=d->pending.right(65536);
 });
 connect(&d->process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error!=QProcess::FailedToStart||!running())return;d->log.close();d->state="failed";emit failed(d->process.errorString());emit stateChanged(d->state);});
 connect(&d->process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
  if(!running())return;d->log.close();
  if(d->cancelled){d->state="cancelled";emit stateChanged(d->state);return;}
  try{
   require(exit==QProcess::NormalExit&&code==0,"Worker failed; inspect worker.log");require(read(d->directory+"/project.json",8*1024*1024)==d->input,"Launch input snapshot changed");require(executableHash(d->options.worker)==d->workerHash,"Worker executable changed during run");
   auto bytes=read(d->directory+"/result.json",64*1024*1024);auto r=Json::parse(bytes.toStdString());require(r.at("schema")=="scdat.radiation-result/2"&&r.at("status")=="completed","Incomplete or incompatible Worker result");
   require(r.at("project_sha256")==digest(d->input).toStdString()&&r.at("worker").at("sha256")==d->workerHash.toStdString()&&r.at("histories")==d->histories&&r.at("project")==d->expectedProject,"Result does not match launch identity");
   const auto& audit=r.at("runtime_audit");require(audit.at("status")=="ready"&&audit.at("manifest_sha256")==d->manifestHash.toStdString()&&audit.at("build_context_sha256")==d->contextHash.toStdString(),"Result runtime differs from launch assets");
   std::set<std::string> expected;auto p=Project::fromJson(d->expectedProject);const auto compiled=spacerad::engine::compileGeometry(p);for(const auto& v:compiled.volumes)if(v.sensitive)expected.insert(v.id);
   if(!d->fluence.is_null())require(read(d->directory+"/fluence-settings.json",65536)==d->fluenceBytes,"Fluence settings snapshot changed");
   spacerad::engine::validateTargetFluence(p,r,d->fluence,d->fluence.is_null()?"":digest(d->fluenceBytes).toStdString());
   spacerad::engine::validateVoxelResult(p,r);
   spacerad::engine::validateImportanceResult(p,r);
   if(!r.at("targets").is_array()||r.at("targets").size()!=expected.size())throw std::runtime_error("Sensitive target count differs: expected="+std::to_string(expected.size())+", actual="+std::to_string(r.at("targets").size())+", IDs="+Json(expected).dump());
   for(const auto& t:r.at("targets")){require(expected.erase(t.at("id").get<std::string>())==1,"Unexpected target ID");for(auto field:{"mass_kg","tid_Gy","tid_standard_error_Gy"}){double value=t.at(field);require(std::isfinite(value)&&value>=0,"Invalid target mass/dose/error");}require(t.at("mass_kg").get<double>()>0,"Target mass must be positive");}
   d->accepted=bytes;d->state="completed";emit completed(bytes);emit stateChanged(d->state);
  }catch(const std::exception& e){d->accepted.clear();d->state="failed";emit failed(QString::fromUtf8(e.what()));emit stateChanged(d->state);}
 });
}
AnalysisService::~AnalysisService(){disconnect(&d->process,nullptr,this,nullptr);if(d->process.state()!=QProcess::NotRunning){d->process.kill();d->process.waitForFinished(3000);}}
void AnalysisService::configure(const RuntimeOptions& options){require(!running(),"Cannot change runtime while running");d->options=options;d->accepted.clear();d->state="ready";emit stateChanged(d->state);}
RuntimeOptions AnalysisService::runtime() const{return d->options;}
void AnalysisService::start(const Project& project,const QString& directory){start(project,directory,nullptr);}
void AnalysisService::start(const Project& project,const QString& directory,const Json& targetFluenceSettings){
 require(!running(),"Analysis already running");project.validate();Json fluence=nullptr;
 if(!targetFluenceSettings.is_null()){auto spec=engine::TargetFluenceSpec::fromJson(targetFluenceSettings);spec.volume(project);fluence=spec.toJson();}
 auto env=childEnvironment(d->options);auto context=read(QFileInfo(d->options.worker).dir().filePath("radiation-build.json"),1024*1024);auto manifest=read(QFileInfo(d->options.worker).dir().filePath("radiation-datasets.txt"),65536);
 require(!directory.trimmed().isEmpty()&&QFileInfo(directory).isAbsolute()&&!QFileInfo::exists(directory),"Choose a new absolute output directory");auto input=QByteArray::fromStdString(project.toJson().dump(2));require(input.size()<=8*1024*1024,"Project exceeds 8 MiB launch budget");auto workerHash=executableHash(d->options.worker);
 require(QDir().mkpath(directory),"Cannot create job directory");QSaveFile f(directory+"/project.json");require(f.open(QIODevice::WriteOnly)&&f.write(input)==input.size()&&f.commit(),"Cannot save project snapshot");
 d->fluence=fluence;d->fluenceBytes.clear();
 if(!fluence.is_null()){
    d->fluenceBytes=QByteArray::fromStdString(fluence.dump(2));QSaveFile settings(directory+"/fluence-settings.json");
    require(settings.open(QIODevice::WriteOnly)&&settings.write(d->fluenceBytes)==d->fluenceBytes.size()&&settings.commit(),"Cannot save fluence settings snapshot");
 }
 d->directory=QFileInfo(directory).absoluteFilePath();d->input=input;d->expectedProject=spacerad::engine::loadProjectFile(directory+"/project.json").project.toJson();d->workerHash=workerHash;d->contextHash=digest(context);d->manifestHash=digest(manifest);d->histories=project.events;d->accepted.clear();d->pending.clear();d->cancelled=false;
 d->log.setFileName(directory+"/worker.log");require(d->log.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create Worker log");d->process.setProcessEnvironment(env);d->process.setWorkingDirectory(d->directory);d->process.setProgram(d->options.worker);QStringList args{d->directory+"/project.json",d->directory+"/result.json"};if(!fluence.is_null()){args.prepend("--target-fluence");args.append(d->directory+"/fluence-settings.json");}d->process.setArguments(args);d->state="running";emit stateChanged(d->state);if(d->cancelled){d->log.close();d->state="cancelled";emit stateChanged(d->state);return;}emit progress(0,project.events);if(d->cancelled){d->log.close();d->state="cancelled";emit stateChanged(d->state);return;}d->process.start();
}
void AnalysisService::cancel(){if(!running())return;d->cancelled=true;d->accepted.clear();d->process.kill();}
bool AnalysisService::running() const{return d->state=="running";}
QString AnalysisService::state() const{return d->state;}
QByteArray AnalysisService::result() const{return d->accepted;}
QString AnalysisService::outputDirectory() const{return d->directory;}
}
