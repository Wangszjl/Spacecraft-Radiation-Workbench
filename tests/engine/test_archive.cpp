#include "RadiationProject.h"
#include "runtime/BatchArchive.h"
#include "runtime/ProjectFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace spacerad::engine;
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F&& f, const char* message) {
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        throw std::runtime_error("cannot write archive fixture");
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read archive fixture");
    return file.readAll();
}
QByteArray jsonBytes(const Json& value) { return QByteArray::fromStdString(value.dump(2)); }
Json readJson(const QString& path) { return Json::parse(readBytes(path).toStdString()); }

Project projectFixture(int events, int seed = 1001) {
    Project project = Project::demo();
    project.name = "Batch archive synthetic fixture";
    project.volumes = {{"Target_Si", "G4_Si", {0, 0, 0}, {80, 80, 80}, 0, true}};
    project.assemblies.clear(); project.templates.clear(); project.instances.clear();
    project.events = events; project.seed = seed;
    project.validate();
    return project;
}
BatchOptions optionsFixture(int maxHistories) {
    BatchOptions options;
    options.historiesPerBatch = 4; options.maxHistories = maxHistories;
    options.maxParallel = 2; options.maxSeconds = 0.0; options.targetRelativeError = 0.1;
    options.minNonzeroHistories = 2; options.minCompletedBatches = 2;
    return options;
}
const Json runtimeFixture = {{"sdk", "synthetic-runtime"}, {"datasets", Json::array({"synthetic-data"})}};
const std::string workerFixture(64, 'a');

Json moment(int count) {
    return {{"count", count}, {"nonzero_histories", count}, {"mean_MeV", 1.0}, {"m2_MeV2", 0.0}};
}
Json childResult(const Project& child, const QByteArray& projectBytes, const Json& runtime) {
    const Json target = {{"id", "Target_Si"}, {"mass_kg", 0.005},
                         {"primary_entry_histories", child.events}};
    const Json audit = {{"contract", "synthetic"}, {"volumes", Json::array({{{"id", "Target_Si"}}})}};
    return {{"schema", "scdat.radiation-result/2"}, {"status", "completed"},
            {"project", child.toJson()}, {"project_sha256", archiveSha(projectBytes)},
            {"runtime_audit", runtime}, {"geometry_audit", audit},
            {"material_audit", {{"status", "synthetic"}, {"snapshot_sha256", std::string(64, 'b')}}},
            {"histories", child.events}, {"expected_primaries", child.expectedPrimaries()},
            {"targets", Json::array({target})},
            {"score_moments", {{"Target_Si", {{"ionizing", moment(child.events)},
                {"total", moment(child.events)}, {"nonionizing", moment(child.events)}}}}},
            {"track_segments", Json::array()}};
}

struct ArchiveFixture { QString directory; Json manifest; Json plan; Json options; QByteArray rootProjectBytes; };
ArchiveFixture makeArchive(const QString& directory, int budget, const std::vector<int>& completedIndices,
                           bool terminal = false, const Json& runtime = runtimeFixture,
                           const std::string& worker = workerFixture) {
    if (!QDir().mkpath(directory)) throw std::runtime_error("cannot create archive fixture directory");
    Project project = projectFixture(budget);
    const QByteArray rootBytes = jsonBytes(project.toJson());
    writeBytes(directory + "/project.json", rootBytes);
    const Json options = optionsFixture(budget).toJson();
    const QByteArray optionsBytes = jsonBytes(options);
    writeBytes(directory + "/options.json", optionsBytes);
    const auto planEntries = planBatches(project, BatchOptions::fromJson(options));
    const Json plan = batchPlanJson(planEntries);
    const QByteArray planBytes = jsonBytes(plan);
    writeBytes(directory + "/plan.json", planBytes);

    Json records = Json::array();
    Json childResults = Json::array();
    for (int index = 0; index < int(plan.size()); ++index) {
        const auto& entry = plan.at(index);
        Json record = entry;
        const QString relative = QString("batch-%1").arg(index, 6, 10, QChar('0'));
        record["directory"] = relative.toStdString();
        if (std::find(completedIndices.begin(), completedIndices.end(), index) != completedIndices.end()) {
            const QString childDir = directory + "/" + relative;
            if (!QDir().mkpath(childDir)) throw std::runtime_error("cannot create child fixture directory");
            Project child = project;
            child.events = entry.at("histories").get<int>();
            child.seed = entry.at("seed").get<int>();
            const QByteArray childProjectBytes = jsonBytes(child.toJson());
            writeBytes(childDir + "/project.json", childProjectBytes);
            const auto loadedChild = loadProjectFile(childDir + "/project.json");
            Json result = childResult(loadedChild.project, childProjectBytes, runtime);
            result["project"] = loadedChild.project.toJson();
            const QByteArray resultBytes = jsonBytes(result);
            const QByteArray logBytes("synthetic child log\n");
            writeBytes(childDir + "/result.json", resultBytes);
            writeBytes(childDir + "/transport.log", logBytes);
            record["status"] = "completed";
            record["project_sha256"] = archiveSha(childProjectBytes);
            record["result_sha256"] = archiveSha(resultBytes);
            record["transport_log_sha256"] = archiveSha(logBytes);
            childResults.push_back(result);
        } else {
            record["status"] = "running";
            record["project_sha256"] = std::string(64, 'c');
        }
        records.push_back(record);
    }

    Json manifest = {{"schema", "scdat.radiation-batch-manifest/2"},
        {"status", terminal ? "completed" : "running"},
        {"project_sha256", archiveSha(rootBytes)}, {"options_sha256", archiveSha(optionsBytes)},
        {"plan_sha256", archiveSha(planBytes)}, {"worker_sha256", worker},
        {"runtime_audit", runtime}, {"plan", plan}, {"batches", records}};
    if (terminal) {
        const auto loaded = loadProjectFile(directory + "/project.json");
        Json final = mergeBatchResults(loaded.project, childResults);
        final["project_sha256"] = archiveSha(rootBytes);
        final["status"] = "completed";
        final["batch"] = {{"options", options}, {"plan", plan}, {"batches", records}};
        const QByteArray finalBytes = jsonBytes(final);
        writeBytes(directory + "/result.json", finalBytes);
        manifest["result_sha256"] = archiveSha(finalBytes);
    }
    writeBytes(directory + "/manifest.json", jsonBytes(manifest));
    return {directory, manifest, plan, options, rootBytes};
}
QString cloneArchive(const QString& source, const QString& baseName) {
    const QString destination = QFileInfo(source).dir().filePath(baseName);
    if (!QDir().mkpath(destination)) throw std::runtime_error("cannot create cloned archive directory");
    const auto files = QDir(source).entryInfoList(QDir::Files);
    for (const auto& file : files) writeBytes(destination + "/" + file.fileName(), readBytes(file.filePath()));
    const auto dirs = QDir(source).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& dir : dirs) {
        if (!QDir().mkpath(destination + "/" + dir.fileName())) throw std::runtime_error("cannot clone child directory");
        const auto childFiles = QDir(dir.filePath()).entryInfoList(QDir::Files);
        for (const auto& file : childFiles)
            writeBytes(destination + "/" + dir.fileName() + "/" + file.fileName(), readBytes(file.filePath()));
    }
    return destination;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "Temporary directory must be available");
        const QString root = temporary.path();
        const auto base = makeArchive(root + "/running-archive", 8, {0});

        {
            auto archive = loadBatchArchive(base.directory);
            check(archive.manifest.at("schema") == "scdat.radiation-batch-manifest/2" &&
                  archive.completed.size() == 1 && archive.completed[0].record.at("index") == 0,
                  "Running checkpoint must load and expose only complete verified child batches");
            check(archive.input.bytes == base.rootProjectBytes && archive.options.toJson() == base.options &&
                  archive.manifest.at("project_sha256") == archiveSha(base.rootProjectBytes),
                  "Root project, options and manifest fingerprints must verify");
            const QString childPath = base.directory + "/batch-000000/project.json";
            check(archive.completed[0].result.at("project") == loadProjectFile(childPath).project.toJson(),
                  "Embedded completed project must equal the strict ProjectFile reload");
            const auto report = batchArchiveReport(archive, runtimeFixture, workerFixture);
            check(report.at("status") == "verified" && report.at("resume_compatible") == true &&
                  report.at("reusable_indices") == Json::array({0}),
                  "Archive report must identify reusable batches and matching runtime fingerprints");
        }

        auto resumeOptions = optionsFixture(12);
        resumeOptions.maxParallel = 4; resumeOptions.maxSeconds = 60;
        resumeOptions.targetRelativeError = 0.2; resumeOptions.minNonzeroHistories = 5;
        {
            auto archive = loadBatchArchive(base.directory);
            const auto resumed = prepareBatchResume(archive, resumeOptions, runtimeFixture, workerFixture);
            check(resumed.project.events == 12 && resumed.project.seed == projectFixture(8).seed &&
                  resumed.project.name == projectFixture(8).name,
                  "Resume may extend aligned history budget while preserving mission and seed");
            check(resumed.project.provenance.at("batch_resume").at("reused_indices") == Json::array({0}) &&
                  resumed.project.provenance.at("workbench_batch_options") == resumeOptions.toJson(),
                  "Resume project must record source identity, reused indices and new options");
        }
        rejects([&] { auto archive = loadBatchArchive(base.directory); (void)prepareBatchResume(archive, resumeOptions, runtimeFixture, std::string(64, 'd')); },
                "Resume must reject a different Worker fingerprint");
        rejects([&] { auto archive = loadBatchArchive(base.directory); (void)prepareBatchResume(archive, resumeOptions, Json{{"sdk", "other"}}, workerFixture); },
                "Resume must reject a changed runtime or dataset configuration");
        auto changedBatch = resumeOptions; changedBatch.historiesPerBatch = 3;
        rejects([&] { auto archive = loadBatchArchive(base.directory); (void)prepareBatchResume(archive, changedBatch, runtimeFixture, workerFixture); },
                "Resume must keep the original fixed batch size");
        auto reducedBudget = optionsFixture(4);
        rejects([&] { auto archive = loadBatchArchive(base.directory); (void)prepareBatchResume(archive, reducedBudget, runtimeFixture, workerFixture); },
                "Resume must not reduce the total history budget");

        const auto tail = makeArchive(root + "/tail-archive", 7, {0, 1});
        auto aligned = optionsFixture(8);
        rejects([&] { auto archive = loadBatchArchive(tail.directory); (void)prepareBatchResume(archive, aligned, runtimeFixture, workerFixture); },
                "Budget extension must reject when it changes an already completed tail batch");

        const auto terminal = makeArchive(root + "/terminal-archive", 4, {0}, true);
        {
            auto archive = loadBatchArchive(terminal.directory);
            check(archive.manifest.at("status") == "completed" && archive.completed.size() == 1 &&
                  archive.manifest.at("result_sha256") == archiveSha(readBytes(terminal.directory + "/result.json")),
                  "Terminal archive must verify final result SHA and terminal state");
        }

        auto badRoot = cloneArchive(base.directory, "bad-root-sha");
        auto badRootManifest = readJson(badRoot + "/manifest.json"); badRootManifest["project_sha256"] = std::string(64, 'e');
        writeBytes(badRoot + "/manifest.json", jsonBytes(badRootManifest));
        rejects([&] { (void)loadBatchArchive(badRoot); }, "Root project SHA mismatch must be rejected");

        auto badChild = cloneArchive(base.directory, "bad-child-sha");
        writeBytes(badChild + "/batch-000000/transport.log", QByteArray("tampered log\n"));
        rejects([&] { (void)loadBatchArchive(badChild); }, "Completed child log SHA mismatch must be rejected");

        auto badEmbedded = cloneArchive(base.directory, "bad-embedded-project");
        auto embeddedResult = readJson(badEmbedded + "/batch-000000/result.json");
        embeddedResult["project"]["seed"] = embeddedResult.at("project").at("seed").get<int>() + 1;
        const QByteArray embeddedBytes = jsonBytes(embeddedResult);
        writeBytes(badEmbedded + "/batch-000000/result.json", embeddedBytes);
        auto embeddedManifest = readJson(badEmbedded + "/manifest.json");
        embeddedManifest["batches"][0]["result_sha256"] = archiveSha(embeddedBytes);
        writeBytes(badEmbedded + "/manifest.json", jsonBytes(embeddedManifest));
        rejects([&] { (void)loadBatchArchive(badEmbedded); },
                "Embedded child project mismatch must be rejected even with refreshed result hash");

        auto badPath = cloneArchive(base.directory, "bad-relative-path");
        auto pathManifest = readJson(badPath + "/manifest.json"); pathManifest["batches"][0]["directory"] = "../outside";
        writeBytes(badPath + "/manifest.json", jsonBytes(pathManifest));
        rejects([&] { (void)loadBatchArchive(badPath); }, "Parent-directory traversal in child path must be rejected");

        auto oldSchema = cloneArchive(base.directory, "old-schema");
        auto oldManifest = readJson(oldSchema + "/manifest.json"); oldManifest["schema"] = "scdat.radiation-batch-manifest/1";
        writeBytes(oldSchema + "/manifest.json", jsonBytes(oldManifest));
        rejects([&] { (void)loadBatchArchive(oldSchema); }, "Manifest schema 1 must be rejected for resume");

        auto locked = cloneArchive(base.directory, "locked-archive");
        {
            QLockFile activeLock(locked + "/job.lock"); activeLock.setStaleLockTime(0);
            check(activeLock.tryLock(0), "Fixture active lock must be acquired");
            rejects([&] { (void)loadBatchArchive(locked); }, "An active cooperative archive lock must block loading");
        }

        const QString compareDir = root + "/compare";
        if (!QDir().mkpath(compareDir)) throw std::runtime_error("cannot create comparison fixture directory");
        auto left = projectFixture(8).toJson();
        const QString leftPath = compareDir + "/left.json";
        writeBytes(leftPath, jsonBytes(left));
        const QString samePath = compareDir + "/same.json";
        writeBytes(samePath, jsonBytes(left));
        auto same = compareProjectFiles(leftPath, samePath);
        check(same.at("normalized_inputs_equal") == true && same.at("transport_inputs_equal") == true &&
              same.at("change_count") == 0,
              "Byte-equivalent projects at different paths must compare equal");

        auto metadata = left; metadata["provenance"]["review_note"] = "metadata only";
        const QString metadataPath = compareDir + "/metadata.json"; writeBytes(metadataPath, jsonBytes(metadata));
        const auto metadataDiff = compareProjectFiles(leftPath, metadataPath);
        check(metadataDiff.at("normalized_inputs_equal") == false && metadataDiff.at("transport_inputs_equal") == true &&
              metadataDiff.at("changes").at(0).at("path") == "/provenance/review_note",
              "Metadata-only change must be visible but not alter transport-input identity");

        auto geometry = left; geometry["volumes"][0]["size_mm"][0] = 81.0;
        const QString geometryPath = compareDir + "/geometry.json"; writeBytes(geometryPath, jsonBytes(geometry));
        const auto geometryDiff = compareProjectFiles(leftPath, geometryPath);
        check(geometryDiff.at("transport_inputs_equal") == false &&
              geometryDiff.at("changes").at(0).at("path").get<std::string>().find("/volumes/0/size_mm") == 0,
              "Geometry differences must be reported at their JSON pointer");

        auto runChanged = left; runChanged["seed"] = left.at("seed").get<int>() + 1; runChanged["events"] = 9;
        const QString runPath = compareDir + "/seed-events.json"; writeBytes(runPath, jsonBytes(runChanged));
        const auto runDiff = compareProjectFiles(leftPath, runPath);
        bool seedPath = false, eventsPath = false;
        for (const auto& change : runDiff.at("changes")) {
            seedPath = seedPath || change.at("path") == "/seed";
            eventsPath = eventsPath || change.at("path") == "/events";
        }
        check(runDiff.at("normalized_inputs_equal") == false && runDiff.at("transport_inputs_equal") == true &&
              seedPath && eventsPath,
              "Seed and event-budget differences must be surfaced but classified outside transport geometry identity");

        std::cout << "Batch archive contract: " << checks
                  << " checks passed; archive integrity, resume compatibility, locking and project comparison\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
