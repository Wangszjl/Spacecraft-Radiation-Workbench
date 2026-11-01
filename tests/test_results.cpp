#include "spacerad/Results.h"
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace spacerad;
namespace {
int checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool failed=false; try { f(); } catch (const std::exception&) { failed=true; } check(failed,message);
}
void write(const QString& path,const QByteArray& bytes) {
    QFile f(path); if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(bytes)!=bytes.size()) throw std::runtime_error("fixture write failed");
}
QByteArray read(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("fixture read failed"); return f.readAll(); }
QByteArray encoded(const Json& j) { return QByteArray::fromStdString(j.dump(2)); }
std::string sha(const QByteArray& b) { return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex().toStdString(); }
struct Fixture { QString dir; Project project; Json result; };
Fixture make(const QString& dir, Project p=Project::demo(), double dose=1.0) {
    if(!QDir().mkpath(dir)) throw std::runtime_error("fixture directory failed");
    p.name="Offline no-transport result fixture";
    const QByteArray projectBytes=encoded(p.toJson()); write(dir+"/project.json",projectBytes);
    const auto loaded=engine::loadProjectFile(dir+"/project.json");
    const auto geometry=engine::compileGeometry(loaded.project);
    Json targets=Json::array();
    for(const auto& placement:geometry.placements) {
        const auto& v=geometry.volumes.at(placement.index); if(!v.sensitive) continue;
        targets.push_back({{"id",v.id},{"mass_kg",1.0},{"tid_Gy",dose},{"tid_rad",dose*100.0},
            {"tid_standard_error_Gy",dose*0.1},{"relative_standard_error",dose>0?Json(0.1):Json(nullptr)},
            {"nonzero_histories",dose>0?1:0}});
    }
    Json r={{"schema","scdat.radiation-result/2"},{"status","completed"},{"histories",loaded.project.events},
        {"expected_primaries",loaded.project.expectedPrimaries()},{"project_sha256",sha(projectBytes)},
        {"project",loaded.project.toJson()},{"worker",{{"sha256",std::string(64,'a')}}},
        {"runtime_audit",{{"status","ready"},{"manifest_sha256",std::string(64,'b')},{"build_context_sha256",std::string(64,'c')}}},
        {"engine_version","synthetic-fixture"},{"capability_status",{{"tid","synthetic"},{"ddd","synthetic"},
            {"see","synthetic"},{"environment_model_execution","synthetic"},{"external_physics_validation","synthetic"}}},
        {"targets",targets}};
    write(dir+"/result.json",encoded(r)); return {dir,loaded.project,r};
}
void rewrite(Fixture& f, const Json& r) { f.result=r; write(f.dir+"/result.json",encoded(r)); }
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir tmp; check(tmp.isValid(),"temporary root must exist");
        Project p=Project::demo();
        auto a=make(tmp.path()+"/a",p,1.25), b=make(tmp.path()+"/b",p,1.25);
        auto run=loadArchivedRun(a.dir);
        check(run.project().name==a.project.name && run.summary().at("verification")=="offline_pair_consistency_only_not_producer_authentication","offline archive loads and summarizes provenance");
        const QString moved=tmp.path()+"/relocated"; QDir().rename(a.dir,moved);
        run=loadArchivedRun(moved); check(run.directory()==QFileInfo(moved).absoluteFilePath(),"archive works after moving directory");
        auto cmp=compareRuns(run,loadArchivedRun(b.dir));
        check(cmp.at("rows").size()==a.result.at("targets").size(),"comparison covers compiled sensitive targets");
        for(const auto& row:cmp.at("rows")) check(row.at("delta_Gy")==0 && row.at("ratio_B_over_A")==1,"identical comparison delta and ratio");
        auto zero=make(tmp.path()+"/zero",p,0.0); auto zcmp=compareRuns(loadArchivedRun(zero.dir),loadArchivedRun(b.dir));
        check(zcmp.at("rows").at(0).at("ratio_B_over_A").is_null(),"zero baseline ratio is undefined");
        Project env=p; env.duration*=2; env.environmentSource+="; changed fixture source";
        auto envRun=make(tmp.path()+"/env",env,1.25); auto ec=compareRuns(loadArchivedRun(b.dir),loadArchivedRun(envRun.dir));
        check(ec.at("same_source_settings")==false,"duration affects source settings");
        Project seed=p; ++seed.seed; auto seedRun=make(tmp.path()+"/seed",seed,1.25);
        check(compareRuns(loadArchivedRun(b.dir),loadArchivedRun(seedRun.dir)).at("same_source_settings")==true,"seed does not alter source settings");
        auto capabilities=make(tmp.path()+"/capabilities",p,1.25);
        auto changedCapabilities=capabilities.result; changedCapabilities["capability_status"]["ddd"]="different-declaration"; rewrite(capabilities,changedCapabilities);
        auto capabilityComparison=compareRuns(loadArchivedRun(b.dir),loadArchivedRun(capabilities.dir));
        check(!capabilityComparison.at("same_physics_assets").get<bool>() && !capabilityComparison.at("rows").at(0).at("controlled_comparison").get<bool>(),"capability declaration mismatch is flagged");
        Project altered=p; altered.volumes.at(2).size[0]+=3.0;
        auto alteredRun=make(tmp.path()+"/altered",altered,1.25);
        auto changed=compareRuns(loadArchivedRun(b.dir),loadArchivedRun(alteredRun.dir));
        bool changedTarget=false; for(const auto& row:changed.at("rows"))
            if(row.at("id")=="OBC_Si") changedTarget=row.at("status")=="target_changed";
        check(changedTarget,"changed target definition is flagged by ID");
        Project added=p; added.volumes.push_back({"Extra_Target","G4_Si",{200,100,0},{10,10,10},0,true});
        auto addedRun=make(tmp.path()+"/added",added,1.25);
        auto unionCmp=compareRuns(loadArchivedRun(b.dir),loadArchivedRun(addedRun.dir));
        bool candidateOnly=false; for(const auto& row:unionCmp.at("rows")) candidateOnly|=row.at("status")=="candidate_only";
        check(candidateOnly && unionCmp.at("rows").size()>cmp.at("rows").size(),"comparison reports union of target IDs");
        auto csv=comparisonCsv(cmp);
        auto csvNull=comparisonCsv(zcmp);
        const auto csvLines=csvNull.split('\n');
        const auto csvFields=csvLines.at(1).split(',');
        check(csv.contains("1.25") && csv.contains("0.0") && csv.startsWith("id,status,controlled_comparison") &&
              csvFields.size()==11 && csvFields.last().isEmpty(),"CSV retains precision, column count and trailing empty null field");
        auto bad=make(tmp.path()+"/bad",p);
        auto mustReject=[&](const char* name, auto mutate) { auto f=make(tmp.path()+"/case-"+name,p); auto j=f.result; mutate(j); rewrite(f,j); rejects([&]{(void)loadArchivedRun(f.dir);},name); };
        rejects([&]{(void)loadArchivedRun(tmp.path()+"/missing");},"missing directory rejected");
        QFile::remove(bad.dir+"/result.json"); rejects([&]{(void)loadArchivedRun(bad.dir);},"missing result rejected");
        mustReject("failed-status",[](Json& j){j["status"]="failed";});
        mustReject("hash",[](Json& j){j["project_sha256"]=std::string(64,'f');});
        mustReject("project-content",[](Json& j){j["project"]["seed"]=j.at("project").at("seed").get<long>()+1;});
        mustReject("duplicate-id",[](Json& j){j["targets"].push_back(j.at("targets").at(0));});
        mustReject("omitted-id",[](Json& j){j["targets"].erase(j["targets"].begin());});
        mustReject("negative-mass",[](Json& j){j["targets"][0]["mass_kg"]=-1;});
        mustReject("negative-dose",[](Json& j){j["targets"][0]["tid_Gy"]=-1;});
        mustReject("overflow-rad",[](Json& j){j["targets"][0]["tid_Gy"]=1e308;j["targets"][0]["tid_rad"]=1e308;});
        mustReject("missing-capabilities",[](Json& j){j.erase("capability_status");});
        mustReject("fractional-hits",[](Json& j){j["targets"][0]["nonzero_histories"]=1.5;});
        mustReject("rse",[](Json& j){j["targets"][0]["relative_standard_error"]=0.2;});
        mustReject("gy-rad",[](Json& j){j["targets"][0]["tid_rad"]=1;});
        auto duplicate=make(tmp.path()+"/duplicate-json",p);
        QByteArray dup=read(duplicate.dir+"/result.json"); dup.replace("\"status\": \"completed\"","\"status\": \"completed\",\n  \"status\": \"completed\""); write(duplicate.dir+"/result.json",dup);
        rejects([&]{(void)loadArchivedRun(duplicate.dir);},"duplicate JSON key rejected");
        std::cout<<"Results API contract: "<<checks<<" checks passed; offline synthetic fixtures only (no transport)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
