// CLI orchestration only; transport, runtime validation and output are separate.
#include "RadiationProject.h"
#include "transport/TransportState.h"
#include "transport/Materials.h"
#include "Geometry.h"
#include "runtime/RadiationRuntime.h"
#include "runtime/ProjectFile.h"
#include "runtime/EnvironmentImport.h"
#include "runtime/GeometryExchange.h"
#include "runtime/TriangleMeshExchange.h"
#include "runtime/MeshExperimentJob.h"
#include "runtime/MeshExperimentStudy.h"
#include "runtime/NonconvexAssembly.h"
#include "runtime/StarFanSeparation.h"
#include "transport/MeshExperimentTransport.h"
#include "runtime/StepConversionReview.h"
#include "runtime/BatchRunner.h"
#include "runtime/BatchArchive.h"
#include "runtime/ReproReport.h"
#include "runtime/ShieldingScanRunner.h"
#include "runtime/VoxelExport.h"
#include "runtime/VoxelStatistics.h"
#include "runtime/ResponseCacheRunner.h"
#include "runtime/M1Package.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <iostream>
using namespace spacerad::engine;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    try {
        if(args.size()==4&&args[1]=="--refresh-nonconvex-origins"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing refreshed task");QFile input(args[2]);if(!input.open(QIODevice::ReadOnly)||input.size()<=0||input.size()>8*1024*1024)throw std::runtime_error("Missing bounded v5/v6 input");auto raw=input.readAll();input.close();std::vector<std::set<std::string>> keys;auto job=Json::parse(raw.toStdString(),[&](int depth,Json::parse_event_t e,Json& v){if(depth>32)throw std::runtime_error("Refresh nesting budget");if(e==Json::parse_event_t::object_start)keys.emplace_back();else if(e==Json::parse_event_t::key){if(!keys.back().insert(v.get<std::string>()).second)throw std::runtime_error("Duplicate refresh key");}else if(e==Json::parse_event_t::object_end)keys.pop_back();return true;});if((job.at("schema")!="scdat.mesh-experiment/5"&&job.at("schema")!="scdat.mesh-experiment/6")||!job.at("parts").is_array()||job.at("parts").size()<2||job.at("parts").size()>16||!job.at("origins").is_array()||job.at("origins").size()!=job.at("parts").size())throw std::runtime_error("Refresh requires a v5/v6 part/origin array");for(size_t i=0;i<job.at("parts").size();++i){if(!job["origins"][i].is_object())throw std::runtime_error("Origin object required");job["origins"][i]["mesh_sha256"]=archiveSha(QByteArray::fromStdString(job["parts"][i].dump()));}auto checked=parseMeshExperiment(QByteArray::fromStdString(job.dump()),true);if(!input.open(QIODevice::ReadOnly)||input.readAll()!=raw)throw std::runtime_error("Refresh input changed");writeResult(args[3],job);bool passed=checked.separationAudit.at("separation_gate_passed");std::cout<<Json({{"inspection_gate_passed",passed},{"transport_allowed",false},{"runtime_geometry_executed",false}}).dump()<<std::endl;return passed?0:2;
        }
        if(args.size()==4&&args[1]=="--make-star-fan-assembly"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing star-fan task");auto job=starFanAssemblyDemo(args[2].toStdString());parseMeshExperiment(QByteArray::fromStdString(job.dump()));writeResult(args[3],job);return 0;
        }
        if(args.size()==4&&args[1]=="--make-nonconvex-assembly"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing nonconvex task");auto job=nonconvexAssemblyDemo(args[2].toStdString());parseMeshExperiment(QByteArray::fromStdString(job.dump()));writeResult(args[3],job);return 0;
        }
        if(args.size()==5&&args[1]=="--make-step-mesh-assembly"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing CAD assembly directory");auto prepared=prepareStepMeshAssembly(args[2],args[3].toStdString());if(!QDir().mkpath(args[4]))throw std::runtime_error("Cannot create CAD assembly directory");writeResult(args[4]+"/job.json",prepared.at("job"));writeResult(args[4]+"/preparation.json",prepared);return 0;
        }
        if(args.size()==7&&args[1]=="--inspect-step-mesh-study"){
            if(QFile::exists(args[6]))throw std::runtime_error("Refusing existing study report");auto report=inspectStepMeshStudy({args[2],args[3],args[4]},args[5]=="-"?QString{}:args[5]);writeResult(args[6],report);return 0;
        }
        if(args.size()==7&&args[1]=="--assess-mesh-experiment-statistics"){
            if(QFile::exists(args[6]))throw std::runtime_error("Refusing existing statistics report");auto job=loadMeshExperiment(args[2]);QFile f(args[3]);if(!f.open(QIODevice::ReadOnly)||f.size()<=0||f.size()>32*1024*1024)throw std::runtime_error("Missing bounded mesh result");auto raw=f.readAll();std::vector<std::set<std::string>> keys;auto result=Json::parse(raw.toStdString(),[&](int depth,Json::parse_event_t e,Json& v){if(depth>64)throw std::runtime_error("Result nesting budget");if(e==Json::parse_event_t::object_start)keys.emplace_back();else if(e==Json::parse_event_t::key){if(!keys.back().insert(v.get<std::string>()).second)throw std::runtime_error("Duplicate result key");}else if(e==Json::parse_event_t::object_end)keys.pop_back();return true;});bool goalOk=false,minOk=false;double goal=args[4].toDouble(&goalOk);int minimum=args[5].toInt(&minOk);if(!goalOk||!minOk)throw std::runtime_error("Numeric statistics goals required");writeResult(args[6],assessMeshExperimentStatistics(job,result,goal,minimum));return 0;
        }
        if(args.size()==5&&args[1]=="--make-step-mesh-experiment"){
            auto prepared=prepareStepMeshExperiment(args[2],args[3].toStdString());if(QFile::exists(args[4])||!QDir().mkpath(args[4]))throw std::runtime_error("STEP experiment requires a new preparation directory");writeResult(args[4]+"/job.json",prepared.at("job"));writeResult(args[4]+"/preparation.json",prepared);return 0;
        }
        if(args.size()==4&&args[1]=="--make-mesh-rotated-experiment"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing rotated job");auto j=meshRotatedExperimentDemo(args[2].toStdString());parseMeshExperiment(QByteArray::fromStdString(j.dump()));writeResult(args[3],j);return 0;
        }
        if(args.size()==4&&args[1]=="--inspect-mesh-experiment"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing experiment inspection");auto job=loadMeshExperiment(args[2],true);Json reviews=Json::array();for(const auto& part:job.parts)reviews.push_back(part.report);bool passed=job.separationAudit.value("separation_gate_passed",true);
            writeResult(args[3],{{"schema","scdat.mesh-experiment-inspection/1"},{"job",job.document},{"job_sha256",archiveSha(job.bytes)},{"part_count",job.parts.size()},{"mesh_reviews",reviews},{"separation_audit",job.separationAudit},{"source_enclosure",job.sourceEnclosure},{"inspection_gate_passed",passed},{"transport_allowed",false},{"runtime_geometry_executed",false}});return passed?0:2;
        }
        if(args.size()==5&&args[1]=="--make-mesh-assembly-experiment"){
            if(args[3]!="shielded"&&args[3]!="bare")throw std::runtime_error("Assembly mode must be shielded or bare");if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing assembly job");auto j=meshAssemblyExperimentDemo(args[2].toStdString(),args[3]=="shielded");parseMeshExperiment(QByteArray::fromStdString(j.dump()));writeResult(args[4],j);return 0;
        }
        if(args.size()==4&&args[1]=="--make-mesh-experiment"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing experiment job");auto j=meshExperimentDemo(args[2].toStdString());parseMeshExperiment(QByteArray::fromStdString(j.dump()));writeResult(args[3],j);return 0;
        }
        if(args.size()==4&&args[1]=="--run-mesh-experiment"){
            auto job=loadMeshExperiment(args[2]);auto runtime=auditRuntime();if(QFile::exists(args[3])||!QDir().mkpath(args[3]))throw std::runtime_error("Experiment requires a new output directory");
            QSaveFile copy(args[3]+"/job.json");if(!copy.open(QIODevice::WriteOnly)||copy.write(job.bytes)!=job.bytes.size()||!copy.commit())throw std::runtime_error("Cannot snapshot experiment input");
            auto result=runMeshExperiment(job,runtime);if(loadMeshExperiment(args[2]).bytes!=job.bytes)throw std::runtime_error("Experiment input changed during run");writeResult(args[3]+"/result.json",result);std::cout<<"SCDAT_MESH_EXPERIMENT_COMPLETED"<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--inspect-step-brep-pairs"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing BRep pair review");auto review=inspectStepBrepPairsFile(args[2],args[3]);writeResult(args[4],review.report);return review.report.at("review_gate_passed")==true?0:2;
        }
        if(args.size()==5&&args[1]=="--inspect-brep-witness"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing BRep witness review");auto review=inspectStepBrepWitnessFile(args[2],args[3]);writeResult(args[4],review.report);return review.report.at("review_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--explain-step-contacts"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing contact evidence report");auto review=inspectStepContactEvidenceFile(args[2]);writeResult(args[3],review.report);return review.report.at("review_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--recheck-step-relations-leaf-bvh"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing STEP relation recheck");auto review=inspectStepRelationsFile(args[2],true);writeResult(args[3],review.report);return review.report.at("review_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--recheck-mesh-leaf-bvh"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing mesh recheck");auto review=inspectTriangleMeshFile(args[2],true);writeResult(args[3],review.report);return review.report.at("geometry_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--inspect-step-assembly"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing STEP assembly report");auto review=inspectStepConversionFile(args[2]);if(!review.audit.contains("assembly_audit"))throw std::runtime_error("XDE assembly audit required; legacy flattened package has no occurrence evidence");writeResult(args[3],review.audit);return review.audit.at("assembly_audit").at("qualified")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--inspect-step-relations"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing STEP relations report");auto review=inspectStepRelationsFile(args[2]);writeResult(args[3],review.report);return review.report.at("review_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--inspect-step-conversion"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing STEP conversion report");
            auto review=inspectStepConversionFile(args[2]);writeResult(args[3],review.audit);return review.audit.at("status")=="review_only"?0:2;
        }
        if(args.size()==4&&args[1]=="--inspect-mesh"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing mesh review output");auto review=inspectTriangleMeshFile(args[2]);writeResult(args[3],review.report);std::cout<<Json({{"status",review.report.at("status")},{"transport_allowed",false}}).dump()<<std::endl;return review.report.at("geometry_gate_passed")==true?0:2;
        }
        if(args.size()==4&&args[1]=="--draft-m1-plan"){
            auto plan=draftM1PlanFile(args[2],args[3]);std::cout<<Json({{"status","draft_created"},{"targets",plan.at("targets").size()}}).dump()<<std::endl;return 0;
        }
        if(args.size()==4&&args[1]=="--make-m1-package"){
            auto audit=exportM1Package(args[2],args[3]);std::cout<<Json({{"status","package_created"},{"readiness",audit.at("readiness")},{"m1_accepted",false},{"blocking_gates",audit.at("blocking_gates")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==3&&args[1]=="--verify-m1-package"){
            auto audit=verifyM1Package(args[2]);std::cout<<Json({{"status","verified"},{"readiness",audit.at("readiness")},{"m1_accepted",false}}).dump()<<std::endl;return 0;
        }
        if(args.size()==6&&args[1]=="--build-response-cache"){
            QFile f(args[3]);if(!f.open(QIODevice::ReadOnly)||f.size()>65536)throw std::runtime_error("Cannot read response options <=64 KiB");
            auto options=ResponseOptions::fromJson(Json::parse(f.readAll().toStdString()));auto result=runResponseCache(loadProjectFile(args[2]),options,args[4],args[5],auditRuntime());
            std::cout<<result.dump()<<std::endl;return result.at("status")=="completed"?0:(result.at("status")=="cancelled"?3:1);
        }
        if(args.size()==3&&args[1]=="--verify-response-cache"){
            auto cache=loadResponseCache(args[2]);std::cout<<Json({{"status","verified"},{"cache_key",cache.manifest.at("cache_key")},{"case_count",cache.manifest.at("case_count")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--apply-response-cache"){
            auto result=applyResponseCache(loadProjectFile(args[2]),args[3],args[4],archiveSha(responseRead(QCoreApplication::applicationFilePath())),auditRuntime());std::cout<<Json({{"status",result.at("status")},{"cache_key",result.at("cache_key")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--export-voxels"){
            auto input=loadProjectFile(args[2]);QFile f(args[3]);if(!f.open(QIODevice::ReadOnly)||f.size()>32*1024*1024)throw std::runtime_error("Cannot read voxel result <=32 MiB");
            auto result=Json::parse(f.readAll().toStdString());if(result.at("project_sha256")!=archiveSha(input.bytes))throw std::runtime_error("Voxel result input SHA mismatch");
            auto a=Project::fromJson(result.at("project")).toJson(),b=input.project.toJson();a.erase("provenance");b.erase("provenance");
            if(a!=b)throw std::runtime_error("Voxel result physical input differs");validateVoxelResult(input.project,result);exportVoxelFieldFiles(result,args[4]);
            std::cout<<Json({{"status","voxels_exported"},{"cells",result.at("voxel_field").at("rows").size()}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--plan-shielding-scan"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing scan plan output");
            QFile f(args[3]);if(!f.open(QIODevice::ReadOnly)||f.size()>65536)throw std::runtime_error("Cannot read scan options <=64 KiB");
            auto options=ShieldingScanOptions::fromJson(Json::parse(f.readAll().toStdString()));auto plan=shieldingPlanJson(loadProjectFile(args[2]).project,options);
            writeResult(args[4],plan);std::cout<<Json({{"status","scan_planned"},{"cases",plan.at("cases").size()}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--shielding-scan"){
            QFile f(args[3]);if(!f.open(QIODevice::ReadOnly)||f.size()>65536)throw std::runtime_error("Cannot read scan options <=64 KiB");
            auto options=ShieldingScanOptions::fromJson(Json::parse(f.readAll().toStdString()));auto result=runShieldingScan(loadProjectFile(args[2]),options,args[4],auditRuntime());
            std::cout<<Json({{"status",result.at("status")},{"curve_rows",result.at("curves").size()}}).dump()<<std::endl;return result.at("status")=="completed"?0:(result.at("status")=="cancelled"?3:1);
        }
        if(args.size()==3&&args[1]=="--verify-shielding-scan"){
            std::cout<<verifyShieldingScan(args[2]).dump()<<std::endl;return 0;
        }
        if(args.size()==4&&args[1]=="--make-report"){
            auto report=exportReproReport(args[2],args[3]);std::cout<<Json({{"status","report_exported"},{"summary",report.at("summary")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==3&&args[1]=="--verify-report"){
            auto report=verifyReproReport(args[2]);std::cout<<Json({{"status","verified"},{"summary",report.at("summary")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==4&&args[1]=="--replay-report"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing replay output directory");auto replay=loadReportReplay(args[2]);
            QFile binary(QCoreApplication::applicationFilePath());if(!binary.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot fingerprint replay Worker");
            auto runtime=auditRuntime();if(archiveSha(binary.readAll())!=replay.workerSha||runtime!=replay.runtime)throw std::runtime_error("Report Worker or runtime configuration differs");
            auto result=replayBatchJob(replay.input,replay.options,replay.indices,args[3],runtime);auto comparison=compareReportReplay(replay.expected,result);
            writeResult(args[3]+"/replay-comparison.json",comparison);std::cout<<comparison.dump()<<std::endl;return result.at("status")=="completed"&&comparison.at("passed")==true?0:1;
        }
        if(args.size()==5&&(args[1]=="--batch"||args[1]=="--resume-batch")){
            QFile f(args[3]);
            if(!f.open(QIODevice::ReadOnly)||f.size()>65536)throw std::runtime_error("Cannot read batch options <=64 KiB");
            auto options=BatchOptions::fromJson(Json::parse(f.readAll().toStdString()));
            auto runtime=auditRuntime();auto result=args[1]=="--batch"?runBatchJob(loadProjectFile(args[2]),options,args[4],runtime):resumeBatchJob(args[2],options,args[4],runtime);
            std::cout<<"SCDAT_BATCH "<<result.at("batch").at("stop_reason")<<std::endl;
            return result.at("status")=="completed"?0:(result.at("status")=="cancelled"?3:1);
        }
        if(args.size()==4&&args[1]=="--inspect-batch"){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing existing archive report");
            QFile binary(QCoreApplication::applicationFilePath());if(!binary.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot fingerprint Worker");
            auto archive=loadBatchArchive(args[2]);writeResult(args[3],batchArchiveReport(archive,auditRuntime(),archiveSha(binary.readAll())));return 0;
        }
        if(args.size()==5&&args[1]=="--compare-projects"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing existing history comparison report");
            writeResult(args[4],compareProjectFiles(args[2],args[3]));return 0;
        }
        if(args.size()==4&&args[1]=="--export-geometry"){
            auto loaded=loadProjectFile(args[2]);auto source=sourceFileIdentity(args[2],loaded.bytes);source["kind"]="project_file";
            exportGeometryBundleFile(loaded.project,args[3],source);
            std::cout<<Json({{"status","geometry_exported"},{"model_sha256",geometryDescriptorSha256(loaded.project)}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&(args[1]=="--import-geometry"||args[1]=="--geometry-report")){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing to overwrite existing geometry import/report output");
            auto base=loadProjectFile(args[2]);
            if(args[1]=="--geometry-report"){
                writeResult(args[4],geometryBundleReport(base.project,args[3]));std::cout<<"SCDAT_GEOMETRY_REPORT"<<std::endl;
            }else{
                auto p=importGeometryBundleFile(base.project,args[3]);saveProjectFile(args[4],p);
                std::cout<<Json({{"status","geometry_imported"},{"output_schema",p.toJson().at("schema")},{"model_sha256",geometryDescriptorSha256(p)}}).dump()<<std::endl;
            }return 0;
        }
        if(args.size()==4&&args[1]=="--expand-instances"){
            auto loaded=loadProjectFile(args[2]);auto geometry=compileGeometry(loaded.project);auto p=loaded.project;
            p.provenance["instance_expansion"]={{"contract","scdat.component-instances/1"},{"origins",geometry.origins},
                {"template_count",p.templates.size()},{"instance_count",p.instances.size()}};
            p.volumes=std::move(geometry.volumes);p.assemblies=std::move(geometry.assemblies);p.templates.clear();p.instances.clear();
            saveProjectFile(args[3],p);std::cout<<Json({{"status","expanded"},{"output_schema",p.toJson().at("schema")}}).dump()<<std::endl;return 0;
        }
        if(args.size()==5&&args[1]=="--export-template"){
            if(QFile::exists(args[4]))throw std::runtime_error("Refusing to overwrite existing template output");
            auto loaded=loadProjectFile(args[2]);
            for(const auto& t:loaded.project.templates)if(t.id==args[3].toStdString()){
                writeResult(args[4],templateToJson(t));std::cout<<"SCDAT_TEMPLATE_EXPORTED"<<std::endl;return 0;}
            throw std::runtime_error("Template ID not found");
        }
        if(args.size()==4&&(args[1]=="--material-report"||args[1]=="--pin-materials")){
            if(QFile::exists(args[3]))throw std::runtime_error("Refusing to overwrite material output; choose a new path");
            auto loaded=loadProjectFile(args[2],args[1]=="--pin-materials");auto runtime=auditRuntime();
            if(args[1]=="--pin-materials"){
                // Explicit refresh operation. A saved definition mismatch is never
                // silently accepted by material reporting or transport.
                loaded.project.materialSnapshot=transport::currentMaterialSnapshot(loaded.project);
                saveProjectFile(args[3],loaded.project);
                std::cout<<Json({{"status","materials_pinned"},{"output_schema",loaded.project.toJson().at("schema")},
                    {"engine_version",runtime.at("engine_version")},{"catalog_names",transport::nistMaterialNames()}}).dump()<<std::endl;
            }else{
                auto report=transport::inspectMaterials(loaded.project);report["runtime_audit"]=runtime;
                report["project_sha256"]=loaded.project.provenance.at("last_loaded_input").at("sha256");
                writeResult(args[3],report);std::cout<<"SCDAT_MATERIAL_REPORT "<<args[3].toStdString()<<std::endl;
            }
            return 0;
        }
        if(args.size()==3&&args[1]=="--inspect-spenvis"){
            QFile f(args[2]);if(!f.open(QIODevice::ReadOnly)||f.size()>10*1024*1024)throw std::runtime_error("Cannot read SPENVIS input <=10 MiB");
            std::cout<<inspectSpenvis(f.readAll()).dump(2)<<std::endl;return 0;
        }
        if(args.size()==6&&args[1]=="--import-spenvis"){
            auto base=loadProjectFile(args[2]);QFile optionsFile(args[4]);
            if(!optionsFile.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open import options JSON");
            auto options=Json::parse(optionsFile.readAll().toStdString());
            auto imported=loadSpenvis(args[3],options);auto p=applyEnvironmentImport(base.project,imported,options.at("synthetic").get<bool>());
            saveProjectFile(args[5],p);std::cout<<Json({{"status","imported"},{"mapping",imported.mapping},{"spectrum_integral",p.integratedSpectrum()}}).dump(2)<<std::endl;return 0;
        }
        if(args.size()==2 && args[1]=="--inspect") {
            std::cout<<auditRuntime().dump(2)<<std::endl;return 0;
        }
        if(args.size()==4&&args[1]=="--migrate"){
            auto loaded=loadProjectFile(args[2]);
            saveProjectFile(args[3],loaded.project);
            std::cout<<Json({{"status","migrated"},{"input_schema",loaded.inputSchema},
                {"output_schema",loaded.project.toJson().at("schema")},{"source_sha256",loaded.project.provenance.at("last_loaded_input").at("sha256")},
                {"quantity",quantityName(loaded.project.quantity)}}).dump(2)<<std::endl;return 0;
        }
        if(args.size()!=3) {
            std::cerr<<"Usage: scdat_radiation_worker project.json result.json | --batch project options NEW_DIRECTORY | --inspect | --migrate input output | --material-report project report | --pin-materials input output | --expand-instances input output | --export-template project id output | --export-geometry project bundle | --import-geometry base bundle output | --geometry-report base bundle output | --inspect-spenvis file | --import-spenvis template source options output\n";return 2;
        }
        if(QFile::exists(args[2])) throw std::runtime_error("Refusing to overwrite existing result; choose a new output path");
        auto loaded=loadProjectFile(args[1]);auto project=std::move(loaded.project);
        auto runtime=auditRuntime();
        auto started=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        auto result=transport::runTransport(project);
        result=addProvenance(std::move(result),project,loaded.bytes,started,runtime);
        result["input_schema"]=loaded.inputSchema;
        writeResult(args[2],result);
        std::cout<<"SCDAT_COMPLETED "<<args[2].toStdString()<<std::endl;return 0;
    } catch(const std::exception& e) { std::cerr<<"SCDAT_ERROR "<<e.what()<<std::endl;return 1; }
}
