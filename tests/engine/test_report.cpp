#include "RadiationProject.h"
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include "runtime/ReproReport.h"
#include "runtime/BatchArchive.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace spacerad::engine;
namespace {
int checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
QByteArray bytes(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read test file"); return f.readAll();
}
void write(const QString& path, const QByteArray& value) {
    QFile f(path); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(value) != value.size())
        throw std::runtime_error("cannot write test file");
}
QByteArray jsonBytes(const Json& value) { return QByteArray::fromStdString(value.dump(2)); }
Json readJson(const QString& path) { return Json::parse(bytes(path).toStdString()); }
void writeJson(const QString& path, const Json& value) { write(path, jsonBytes(value)); }
Json moment(int n, int nonzero, double mean, double m2) {
    return {{"count",n},{"nonzero_histories",nonzero},{"mean_MeV",mean},{"m2_MeV2",m2}};
}
struct Fixture { QString job; Project project; QByteArray projectBytes; Json result; };
Fixture makeJob(const QString& root, bool workerPresent = true) {
    Fixture f; f.job = root + "/job"; QDir().mkpath(f.job);
    f.project = Project::demo(); f.project.name = "<script>alert('X')</script> & synthetic";
    f.project.volumes = {{"<img src=x onerror=1>","G4_Si",{0,0,0},{80,80,80},0,true}};
    f.project.assemblies.clear(); f.project.templates.clear(); f.project.instances.clear();
    f.project.events = 4; f.project.seed = 20261020; f.project.validate();
    saveProjectFile(f.job + "/project.json", f.project, false);
    f.projectBytes = bytes(f.job + "/project.json");
    const auto cpu = geometrySnapshot(f.project);
    Json audit = cpu;
    for (auto& row : audit["volumes"]) {
        const double volume = row.at("material_volume_mm3").get<double>();
        const double density = 2330.0;
        row["geant4_material_volume_mm3"] = volume;
        row["density_kg_m3"] = density;
        row["mass_kg"] = volume * density / 1e9;
        row["geant4_world_center_mm"] = row.at("world_center_mm");
        row["geant4_world_rotation"] = row.at("world_rotation");
    }
    const Json runtime = {{"engine_version","synthetic-engine"},{"dataset_environment",Json::object()},
        {"build_configuration",{{"paths",{{"qt_prefix","C:/synthetic/Qt"},{"worker_directory","C:/synthetic"}}}}}};
    const Json target = {{"id","<img src=x onerror=1>"},{"mass_kg",audit.at("volumes").at(0).at("mass_kg")},
        {"primary_entry_histories",2}};
    const Json child = {{"schema","scdat.radiation-result/2"},{"status","completed"},
        {"project",f.project.toJson()},{"project_sha256",archiveSha(f.projectBytes)},
        {"runtime_audit",runtime},{"geometry_audit",audit},{"material_audit",{{"status","synthetic_fixture"},{"snapshot_sha256",std::string(64,'b')}}},
        {"expected_primaries",f.project.expectedPrimaries()},{"histories",4},
        {"targets",Json::array({target})},{"track_segments",Json::array({{{"event",0},{"points_mm",Json::array({{0,0,0},{1,0,0}})}}})},
        {"score_moments",{{"<img src=x onerror=1>",{{"ionizing",moment(4,2,1.0,2.0)},
            {"total",moment(4,2,1.2,2.1)},{"nonionizing",moment(4,1,0.2,0.3)}}}}}};
    f.result = mergeBatchResults(f.project,{child});
    f.result["project_sha256"] = archiveSha(f.projectBytes);
    f.result["requested_configuration"] = {{"threads",1},{"cut_mm",0.01}};
    f.result["effective_configuration"] = {{"threads",1},{"cut_mm",0.01}};
    f.result["engine"] = "synthetic-test-engine"; f.result["engine_version"] = "fixture-1";
    if (workerPresent) f.result["worker"] = {{"sha256",std::string(64,'a')},{"path","C:/synthetic/worker.exe"}};
    else f.result.erase("worker");
    writeJson(f.job + "/result.json",f.result);
    return f;
}
QString createPackage(const Fixture& f,const QString& path) { exportReproReport(f.job,path); return path; }
void refreshManifestHash(const QString& dir,const std::string& name) {
    auto manifest=readJson(dir+"/package-manifest.json");
    for(auto& item:manifest["files"]) if(item.at("path")==name) {
        const auto raw=bytes(dir+"/"+QString::fromStdString(name)); item["size_bytes"]=raw.size();item["sha256"]=archiveSha(raw);writeJson(dir+"/package-manifest.json",manifest);return;
    }
    throw std::runtime_error("manifest entry missing");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temp; check(temp.isValid(),"temporary report directory must be available");
        const auto fixture=makeJob(temp.path()); const auto sourceBefore=bytes(fixture.job+"/project.json");
        const auto package=temp.path()+"/report"; const auto report=exportReproReport(fixture.job,package);
        check(report.at("summary").at("synthetic")==true&&report.at("replay").at("eligible")==true,
              "synthetic serial result should be explicitly labelled and replay eligible");
        check(bytes(fixture.job+"/project.json")==sourceBefore&&report.at("source").at("project_identity").at("sha256")==archiveSha(sourceBefore),
              "export must preserve source bytes and bind raw project identity");
        check(bytes(package+"/report.html").contains("外部物理验证未完成")&&
              !bytes(package+"/report.html").contains("<script>alert")&&
              bytes(package+"/report.html").contains("&lt;script&gt;"),
              "offline report must state physics limitation and escape untrusted project text");
        check(bytes(package+"/report.html").contains("default-src &#39;none&#39;")||bytes(package+"/report.html").contains("default-src 'none'"),
              "HTML must use a restrictive offline CSP");
        check(verifyReproReport(package).at("schema")=="scdat.radiation-repro-report/1"&&
              loadReportReplay(package).indices==std::vector<int>{0},"valid package must verify and load one serial replay index");

        auto zeroFixture=makeJob(temp.path()+"/zero");
        Json zeroChild=zeroFixture.result;
        zeroChild["score_moments"]["<img src=x onerror=1>"]["ionizing"]=moment(4,0,0,0);
        zeroChild["score_moments"]["<img src=x onerror=1>"]["total"]=moment(4,0,0,0);
        zeroChild["score_moments"]["<img src=x onerror=1>"]["nonionizing"]=moment(4,0,0,0);
        zeroChild["targets"][0]={{"id","<img src=x onerror=1>"},{"mass_kg",zeroFixture.result.at("targets").at(0).at("mass_kg")},
            {"primary_entry_histories",0}};
        zeroFixture.result=mergeBatchResults(zeroFixture.project,{zeroChild});
        zeroFixture.result["project_sha256"]=archiveSha(zeroFixture.projectBytes);
        zeroFixture.result["requested_configuration"]={{"threads",1},{"cut_mm",0.01}};
        zeroFixture.result["effective_configuration"]={{"threads",1},{"cut_mm",0.01}};
        zeroFixture.result["engine"]="synthetic-test-engine";zeroFixture.result["engine_version"]="fixture-1";
        zeroFixture.result["worker"]={{"sha256",std::string(64,'a')},{"path","C:/synthetic/worker.exe"}};
        writeJson(zeroFixture.job+"/result.json",zeroFixture.result);
        const auto zeroPackage=temp.path()+"/zero-report";auto zeroReport=exportReproReport(zeroFixture.job,zeroPackage);
        check(zeroReport.at("statistics").at("zero_hit_targets").size()==1&&
              bytes(zeroPackage+"/report.html").contains("不可估计"),
              "zero-hit/null RSE must remain explicitly unestimable, not imply convergence");
        check(zeroReport.at("summary").at("statistical_state")=="not_configured",
              "serial report must not claim batch convergence");

        rejects([&]{exportReproReport(fixture.job,package);},"export must refuse an existing output directory");
        const auto broken=temp.path()+"/broken";QDir().mkpath(broken);write(broken+"/project.json",QByteArray("{"));
        rejects([&]{exportReproReport(broken,temp.path()+"/broken-report");},"damaged source input must be rejected");
        check(!QFileInfo::exists(temp.path()+"/broken-report"),"failed export must not publish a partial output directory");
        auto badPlacement=makeJob(temp.path()+"/placement");badPlacement.result["geometry_audit"]["volumes"][0]["geant4_world_center_mm"][0]=1.0;
        writeJson(badPlacement.job+"/result.json",badPlacement.result);
        rejects([&]{exportReproReport(badPlacement.job,temp.path()+"/bad-placement-report");},"actual Geant4 placement must agree with CPU model");
        check(!QFileInfo::exists(temp.path()+"/bad-placement-report"),"inconsistent geometry audit cannot publish a report");

        auto badPathPackage=temp.path()+"/bad-path";createPackage(fixture,badPathPackage);
        auto manifest=readJson(badPathPackage+"/package-manifest.json");manifest["files"][0]["path"]="../outside";writeJson(badPathPackage+"/package-manifest.json",manifest);
        rejects([&]{verifyReproReport(badPathPackage);},"manifest traversal path must be rejected");
        auto badHashPackage=temp.path()+"/bad-hash";createPackage(fixture,badHashPackage);
        manifest=readJson(badHashPackage+"/package-manifest.json");manifest["files"][0]["sha256"]=std::string(64,'f');writeJson(badHashPackage+"/package-manifest.json",manifest);
        rejects([&]{verifyReproReport(badHashPackage);},"file hash mismatch must be rejected");

        auto legacy=makeJob(temp.path()+"/legacy",false);auto legacyPackage=temp.path()+"/legacy-report";
        const auto legacyReport=exportReproReport(legacy.job,legacyPackage);
        check(legacyReport.at("replay").at("eligible")==false&&verifyReproReport(legacyPackage).at("summary").at("kind")=="serial",
              "legacy report without Worker SHA should remain readable but not replayable");
        rejects([&]{loadReportReplay(legacyPackage);},"legacy report without Worker fingerprint must not replay");

        const auto expected=readJson(package+"/evidence/result.json");
        auto altered=expected;altered["histories"]=3;
        check(compareReportReplay(expected,altered).at("passed")==false,
              "replay comparison must detect changed histories");

        for (const auto& defect : std::vector<std::pair<QString,Json>>{
                 {"bad-index-type",Json("0")},{"bad-index-range",Json(99)},
                 {"bad-index-duplicate",Json::array({0,0})},{"bad-count",Json::array({0})}}) {
            const auto dir=temp.path()+"/"+defect.first;createPackage(fixture,dir);
            auto replay=readJson(dir+"/replay.json");replay["indices"]=defect.second.is_array()?defect.second:Json::array({defect.second});
            if(defect.first=="bad-count") replay["expected_histories"]=5;
            writeJson(dir+"/replay.json",replay);refreshManifestHash(dir,"replay.json");
            rejects([&]{loadReportReplay(dir);},"malformed replay indices/count must be rejected after valid package rehash");
        }

        std::cout<<"Repro report I/O contract: "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
