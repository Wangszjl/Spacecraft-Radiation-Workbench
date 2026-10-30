#include "M1Package.h"
#include "ReproReport.h"
#include "ReportHtml.h"
#include "Materials.h"
#include "ProjectFile.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <set>
#include <map>
#include <algorithm>
#include <stdexcept>
namespace spacerad::engine {
namespace {
constexpr qint64 fileLimit=32*1024*1024,totalLimit=256*1024*1024;
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("M1 package: ")+m);}
QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=fileLimit,"missing/empty/oversized file");auto b=f.read(fileLimit+1);require(f.error()==QFileDevice::NoError&&b.size()<=fileLimit,"file read failed");return b;}
QByteArray inside(const QString& root,const QString& relative){require(!relative.isEmpty()&&!relative.contains('\\')&&!QDir::isAbsolutePath(relative)&&QDir::cleanPath(relative)==relative&&!relative.startsWith("../"),"unsafe package path");auto path=root+"/"+relative,canonical=QFileInfo(path).canonicalFilePath();
#ifdef Q_OS_WIN
    require(path.compare(canonical,Qt::CaseInsensitive)==0,"redirected package artifact");
#else
    require(path==canonical,"redirected package artifact");
#endif
    return read(path);
}
QByteArray bytes(const Json& j){return QByteArray::fromStdString(j.dump(2));}
void write(const QString& path,const QByteArray& b){require(!b.isEmpty()&&b.size()<=fileLimit&&QDir().mkpath(QFileInfo(path).absolutePath()),"cannot prepare new bounded file");QTemporaryFile f(QFileInfo(path).absolutePath()+"/.m1-file-XXXXXX");require(f.open()&&f.write(b)==b.size()&&f.flush(),"cannot write package file");f.close();require(f.rename(path),"refusing existing/unwritable package file");f.setAutoRemove(false);}
QString reportPath(int i){return i==0?"runs/baseline":QString("runs/case-%1").arg(i-1,3,10,QChar('0'));}
Json portable(Json plan){plan["baseline_job"]="runs/baseline/evidence";for(std::size_t i=0;i<plan["sensitivity"].size();++i)plan["sensitivity"][i]["job_directory"]=(reportPath(int(i)+1)+"/evidence").toStdString();for(const auto& key:{"environment","geometry","physics","source"}){auto& review=plan["reviews"][key];if(review.is_null())continue;for(std::size_t i=0;i<review["files"].size();++i)review["files"][i]["path"]="attachments/"+std::string(key)+"-"+std::to_string(i)+".bin";}return plan;}
Json normalizedReport(Json report){report.erase("generated_utc");report["source"].erase("job_directory");if(report["source"].contains("archive_identity")&&report["source"]["archive_identity"].is_object())report["source"]["archive_identity"].erase("directory");return report;}
Json inventory(const QString& root){QStringList names;QDirIterator it(root,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);int entries=0;qint64 size=0;
    while(it.hasNext()){it.next();require(++entries<=50000,"too many package entries");auto f=it.fileInfo();require(!f.isSymLink(),"symbolic link in package");auto rel=QDir(root).relativeFilePath(f.absoluteFilePath());auto path=f.canonicalFilePath();
#ifdef Q_OS_WIN
        require(path.compare(root+"/"+rel,Qt::CaseInsensitive)==0,"redirected package entry");
#else
        require(path==root+"/"+rel,"redirected package entry");
#endif
        if(f.isDir()||rel=="manifest.json")continue;auto b=inside(root,rel);size+=b.size();require(size<=totalLimit,"package exceeds 256 MiB");names.push_back(rel);
    }names.sort();Json files=Json::array();for(const auto& name:names){auto b=inside(root,name);files.push_back({{"path",name.toStdString()},{"size_bytes",b.size()},{"sha256",archiveSha(b)}});}return files;
}
Json sortedSnapshot(Json snapshot){auto& materials=snapshot["materials"];for(auto& material:materials){auto& elements=material["elements"];std::sort(elements.begin(),elements.end(),[](const Json& a,const Json& b){return a.at("Z").get<int>()<b.at("Z").get<int>();});}std::sort(materials.begin(),materials.end(),[](const Json& a,const Json& b){return a.at("id").get<std::string>()<b.at("id").get<std::string>();});return snapshot;}
void validateMaterials(const Project& p,const Json& result){const auto& audit=result.at("material_audit");const auto& snapshot=audit.at("snapshot");auto checked=p;checked.materialSnapshot=snapshot;validateMaterialSnapshot(checked);require(audit.at("snapshot_sha256")==archiveSha(QByteArray::fromStdString(snapshot.dump())),"material snapshot SHA differs from definitions");require(snapshot.at("engine_version")==result.at("engine_version"),"material engine version differs");
    require(audit.at("status")==(!p.materialSnapshot.is_null()?"pinned_match":"resolved_at_run_unpinned"),"material pin status differs");if(!p.materialSnapshot.is_null())require(sortedSnapshot(p.materialSnapshot)==sortedSnapshot(snapshot),"pinned material definitions differ from audit");std::map<std::string,Json> densities;for(const auto& row:snapshot.at("materials"))densities.emplace(row.at("id").get<std::string>(),row.at("density_kg_m3"));for(const auto& v:result.at("geometry_audit").at("volumes")){auto name=v.at("material").get<std::string>();require(densities.count(name)&&densities.at(name)==v.at("density_kg_m3"),"material density differs from geometry audit");}}
M1Run loadRun(const QString& directory){auto report=verifyReproReport(directory);auto input=loadProjectFile(directory+"/evidence/project.json");auto result=Json::parse(read(directory+"/evidence/result.json").toStdString());
    if(result.at("histories").get<int>()>0)validateMaterials(input.project,result);
    if(result.contains("batch")){auto archive=loadBatchArchive(directory+"/evidence");for(const auto& child:archive.completed){require(child.result.at("worker").at("sha256")==result.at("worker").at("sha256"),"child Worker differs from root result");for(const char* key:{"geometry_audit","material_audit","runtime_audit"})require(child.result.at(key)==result.at(key),"child transport audit differs from root result");}}
    QTemporaryDir temporary;require(temporary.isValid(),"cannot create report verification staging");auto rebuilt=exportReproReport(directory+"/evidence",temporary.path()+"/report");require(normalizedReport(report)==normalizedReport(rebuilt),"report semantic summary differs from original transport");require(read(directory+"/report.html")==renderReproHtml(report).toUtf8(),"report HTML differs from validated report data");for(const char* name:{"replay.json","reproduce.ps1"})require(read(directory+"/"+name)==read(temporary.path()+"/report/"+name),"replay artifact differs from rebuilt report");return {std::move(input.project),std::move(result),std::move(report)};
}
Json auditAt(const QString& root){auto sourceBytes=inside(root,"source-plan.json"),planBytes=inside(root,"plan.json");require(sourceBytes.size()<=1024*1024&&planBytes.size()<=1024*1024,"plan exceeds 1 MiB");auto original=validateM1Plan(Json::parse(sourceBytes.toStdString())),plan=validateM1Plan(Json::parse(planBytes.toStdString()));require(plan==portable(original),"portable plan differs from source plan");std::vector<M1Run> runs;for(std::size_t i=0;i<=plan.at("sensitivity").size();++i)runs.push_back(loadRun(root+"/"+reportPath(int(i))));Json identities=Json::object();for(const auto& key:{"environment","geometry","physics","source"}){const auto& review=plan.at("reviews").at(key);if(review.is_null())continue;for(const auto& f:review.at("files")){auto path=f.at("path").get<std::string>();auto b=inside(root,QString::fromStdString(path));identities[path]=sourceFileIdentity(QString::fromStdString(path),b);}}
    auto audit=evaluateM1(plan,runs,identities);audit["source_plan_sha256"]=archiveSha(inside(root,"source-plan.json"));audit["plan_sha256"]=archiveSha(inside(root,"plan.json"));return audit;
}
QString resolve(const QString& parent,const Json& path){auto text=QString::fromStdString(path.get<std::string>());return QFileInfo(QDir::isAbsolutePath(text)?text:QDir(parent).filePath(text)).absoluteFilePath();}
}
Json draftM1PlanFile(const QString& requested,const QString& output){require(!QFileInfo::exists(output),"refusing existing draft");auto job=QFileInfo(requested).canonicalFilePath();require(QFileInfo(job).isDir(),"baseline job unavailable");if(QFileInfo::exists(job+"/batches/manifest.json"))job=QFileInfo(job+"/batches").canonicalFilePath();auto input=loadProjectFile(job+"/project.json");auto plan=draftM1Plan(input.project,job.toStdString());write(output,bytes(plan));return plan;}
Json exportM1Package(const QString& requested,const QString& output){require(!QFileInfo::exists(output)&&QFileInfo(QFileInfo(output).absolutePath()).isDir(),"output must be new under existing parent");auto raw=read(requested);require(raw.size()<=1024*1024,"plan exceeds 1 MiB");auto source=validateM1Plan(Json::parse(raw.toStdString())),plan=portable(source);auto parent=QFileInfo(requested).absolutePath();QTemporaryDir stage(QFileInfo(output).absolutePath()+"/.m1-XXXXXX");require(stage.isValid(),"cannot stage M1 package");
    write(stage.path()+"/source-plan.json",raw);write(stage.path()+"/plan.json",bytes(plan));require(QDir().mkpath(stage.path()+"/runs"),"cannot create run snapshots");qint64 size=raw.size();for(std::size_t i=0;i<=source.at("sensitivity").size();++i){auto job=i==0?source.at("baseline_job"):source.at("sensitivity")[i-1].at("job_directory");exportReproReport(resolve(parent,job),stage.path()+"/"+reportPath(int(i)));auto files=inventory(stage.path());size=0;for(const auto& f:files)size+=f.at("size_bytes").get<qint64>();require(size<=totalLimit,"M1 snapshots exceed package budget");}
    for(const auto& key:{"environment","geometry","physics","source"}){const auto& review=source.at("reviews").at(key);if(review.is_null())continue;for(std::size_t i=0;i<review.at("files").size();++i){auto b=read(resolve(parent,review.at("files")[i].at("path")));size+=b.size();require(size<=totalLimit,"M1 attachments exceed package budget");auto path=QString::fromStdString(plan.at("reviews").at(key).at("files")[i].at("path"));write(stage.path()+"/"+path,b);}}
    auto audit=auditAt(stage.path());write(stage.path()+"/audit.json",bytes(audit));write(stage.path()+"/audit.html",renderM1Html(audit).toUtf8());write(stage.path()+"/manifest.json",bytes({{"schema","scdat.radiation-m1-package/1"},{"files",inventory(stage.path())}}));verifyM1Package(stage.path());require(!QFileInfo::exists(output)&&QDir().rename(stage.path(),QFileInfo(output).absoluteFilePath()),"atomic package publication failed");stage.setAutoRemove(false);return audit;
}
Json verifyM1Package(const QString& directory){auto root=QFileInfo(directory).canonicalFilePath();require(QFileInfo(root).isDir(),"package unavailable");auto manifest=Json::parse(inside(root,"manifest.json").toStdString());require(manifest.is_object()&&manifest.size()==2&&manifest.at("schema")=="scdat.radiation-m1-package/1"&&manifest.at("files").is_array()&&manifest.at("files").size()<=50000,"invalid M1 manifest");std::set<std::string> paths;qint64 total=0;
    for(const auto& f:manifest.at("files")){require(f.is_object()&&f.size()==3,"invalid artifact record");auto path=f.at("path").get<std::string>();require(path!="manifest.json"&&paths.insert(path).second,"duplicate/self-referential artifact");auto b=inside(root,QString::fromStdString(path));total+=b.size();require(total<=totalLimit&&f.at("sha256")==archiveSha(b)&&f.at("size_bytes")==b.size(),"package artifact SHA/size mismatch");}require(manifest.at("files")==inventory(root),"unrecorded or missing package files");auto audit=auditAt(root);require(Json::parse(inside(root,"audit.json").toStdString())==audit&&inside(root,"audit.html")==renderM1Html(audit).toUtf8(),"M1 summary/HTML differs from rebuilt evidence");return audit;
}
QString renderM1Html(const Json& a){auto esc=[](const Json& v){return QString::fromStdString(v.is_string()?v.get<std::string>():v.dump()).toHtmlEscaped();};QString html="<!doctype html><html lang='zh-CN'><meta charset='utf-8'><title>M1 验收准备</title><style>body{font:15px sans-serif;max-width:1200px;margin:32px auto;padding:0 20px;color:#203040}table{border-collapse:collapse;width:100%;margin:20px 0}td,th{padding:9px;border:1px solid #ccd6df;text-align:left;overflow-wrap:anywhere}.fail{background:#fff1d4}.ok{background:#e5f3ee}h1{color:#145775}@media print{body{margin:0;font-size:11px}}</style><h1>"+esc(a.at("name"))+"</h1><p><b>准备度："+esc(a.at("readiness"))+" · M1 未签核</b></p><p>哈希和审核声明不认证真实任务或物理有效性；ready_for_review 仍须人工评审。</p><h2>验收条件</h2><table><tr><th>检查</th><th>状态</th><th>说明</th></tr>";
    for(const auto& g:a.at("gates"))html+="<tr class='"+QString(g.at("passed")==true?"ok":"fail")+"'><td>"+esc(g.at("id"))+"</td><td>"+QString(g.at("passed")==true?"通过":"缺项 / 未通过")+"</td><td>"+esc(g.at("detail"))+"</td></tr>";html+="</table><h2>逐靶条件</h2><table><tr><th>敏感体</th><th>状态</th><th>结果（Gy、kg、MC 统计）</th></tr>";
    for(const auto& t:a.at("target_checks"))html+="<tr><td>"+esc(t.at("id"))+"</td><td>"+QString(t.at("passed")==true?"通过":"不足 / 缺失")+"</td><td>"+esc(t.at("result"))+"</td></tr>";html+="</table><h2>敏感性筛查</h2><p>"+esc(a.at("screening_rule"))+"</p><table><tr><th>案例 / 靶</th><th>类别</th><th>相对变化</th><th>筛查包络 / 阈值</th><th>状态</th></tr>";
    for(const auto& c:a.at("sensitivity_checks"))html+="<tr><td>"+esc(c.at("case_id"))+" / "+esc(c.at("target_id"))+"</td><td>"+esc(c.at("category"))+"</td><td>"+esc(c.at("relative_change"))+"</td><td>"+esc(c.at("screening_envelope"))+" / "+esc(c.at("max_relative_change"))+"</td><td>"+QString(c.at("passed")==true?"通过":"不足 / 未通过")+"</td></tr>";html+="</table><h2>边界</h2><ul>";for(const auto& t:a.at("limitations"))html+="<li>"+esc(t)+"</li>";return html+"</ul></html>";
}
}
