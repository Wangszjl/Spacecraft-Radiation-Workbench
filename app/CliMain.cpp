#include "spacerad/AnalysisService.h"
#include "spacerad/Results.h"
#include "spacerad/ImportanceReview.h"
#include "spacerad/ShieldingScan.h"
#include "spacerad/DeviceResponse.h"
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include "runtime/TargetFluence.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <csignal>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace spacerad;
volatile std::sig_atomic_t interrupted = 0;
void interruptHandler(int signal) { interrupted = signal; }
int interruptExit() { return interrupted == SIGTERM ? 143 : 130; }
std::string utf8(const QString& value) { return value.toUtf8().toStdString(); }
struct Reporter {
    unsigned sequence = 0;
    void event(const char* type, Json fields = Json::object()) {
        fields["schema"] = "spacerad.cli-event/1";
        fields["type"] = type;
        fields["sequence"] = sequence++;
        // ASCII JSON also works with Windows consoles that use legacy codepages.
        std::cout << fields.dump(-1, ' ', true) << '\n' << std::flush;
        if (!std::cout) interrupted = SIGTERM;
    }
    int error(const QString& message, int code) {
        event("error", {{"message", utf8(message)}, {"exit_code", code}});
        return code;
    }
};
QByteArray fingerprint(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot fingerprint runtime asset");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) throw std::runtime_error("Cannot read runtime asset");
    return hash.result().toHex();
}
QString runtimePath(const QCommandLineParser& parser) {
    const auto path = parser.isSet("runtime") ? parser.value("runtime") :
        QCoreApplication::applicationDirPath() + "/runtime.json";
    if (path.trimmed().isEmpty()) throw std::runtime_error("Runtime configuration path is required");
    return QFileInfo(path).absoluteFilePath();
}
engine::LoadedProject loadInput(const QCommandLineParser& parser) {
    const auto path = parser.value("project");
    const QFileInfo file(path);
    if (path.trimmed().isEmpty() || !file.isFile() || file.size() <= 0 || file.size() > 8 * 1024 * 1024)
        throw std::runtime_error("Configure an existing project file <=8 MiB");
    auto loaded = engine::loadProjectFile(file.absoluteFilePath());
    if (parser.isSet("histories")) {
        bool ok = false;
        const auto count = parser.value("histories").toLongLong(&ok);
        if (!ok || count < 2 || count > 100000000) throw std::runtime_error("Histories must be 2..100000000");
        loaded.project.events = int(count);
        loaded.project.validate();
    }
    return loaded;
}
ShieldingScanSettings scanSettings(const QCommandLineParser& parser) {
    QFile file(parser.value("scan-settings"));
    if(!file.open(QIODevice::ReadOnly) || file.size()<=0 || file.size()>65536)throw std::runtime_error("Configure scan settings <=64 KiB");
    const auto raw=file.read(65537);
    if(file.error()!=QFileDevice::NoError || raw.size()>65536)throw std::runtime_error("Cannot read bounded scan settings");
    std::vector<std::set<std::string>> keys;
    const auto json=Json::parse(raw.toStdString(),[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>16)throw std::runtime_error("Scan settings nesting exceeds 16");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key){if(!keys.back().insert(value.get<std::string>()).second)throw std::runtime_error("Duplicate scan settings key");}
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
    return ShieldingScanSettings::fromJson(json);
}
int scan(QCoreApplication& app,const QCommandLineParser& parser,Reporter& reporter) {
    const auto loaded=loadInput(parser);const auto settings=scanSettings(parser);
    const auto plan=planShieldingScan(loaded.project,settings);
    if(parser.value("output").trimmed().isEmpty())throw std::runtime_error("A new scan output directory is required");
    const auto output=QFileInfo(parser.value("output")).absoluteFilePath();
    const auto runtime=loadRuntimeFile(runtimePath(parser));AnalysisService::childEnvironment(runtime);
    int seconds=0;
    if(parser.isSet("timeout-seconds")){bool ok=false;seconds=parser.value("timeout-seconds").toInt(&ok);if(!ok||seconds<0||seconds>86400)throw std::runtime_error("Timeout must be 0..86400 seconds");}
    ShieldingScanService service;service.configure(runtime);bool terminal=false;int requestedExit=0;
    QTimer timeout,interrupts;timeout.setSingleShot(true);interrupts.setInterval(50);
    QObject::connect(&service,&ShieldingScanService::stateChanged,&app,[&](const QString& state){
        if(terminal)return;
        if(state=="cancelled"){
            terminal=true;const int code=requestedExit?requestedExit:interruptExit();
            reporter.event("cancelled",{{"exit_code",code},{"reason",code==124?"timeout":"interrupted"},{"output_directory",utf8(output)}});app.exit(code);
        }else reporter.event("state",{{"state",utf8(state)}});
    });
    QObject::connect(&service,&ShieldingScanService::progress,&app,[&](int done,int total,int histories,int budget){if(!terminal)reporter.event("progress",{{"completed_cases",done},{"total_cases",total},{"histories",histories},{"total_histories",budget}});});
    QObject::connect(&service,&ShieldingScanService::logMessage,&app,[&](const QString& text){if(parser.isSet("verbose"))std::cerr<<utf8(text)<<std::flush;});
    QObject::connect(&service,&ShieldingScanService::failed,&app,[&](const QString& error){if(terminal)return;terminal=true;reporter.error(error,3);app.exit(3);});
    QObject::connect(&service,&ShieldingScanService::completed,&app,[&](const QByteArray& raw){
        if(terminal)return;
        try {const auto result=Json::parse(raw.toStdString());terminal=true;reporter.event("scan-completed",{{"exit_code",0},{"output_directory",utf8(output)},
            {"report",result},{"report_sha256",QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex().toStdString()}});app.exit(0);}
        catch(const std::exception& error){terminal=true;reporter.error(QString::fromUtf8(error.what()),3);app.exit(3);}
    });
    QObject::connect(&timeout,&QTimer::timeout,&app,[&]{if(!terminal&&service.running()){requestedExit=124;service.cancel();}});
    QObject::connect(&interrupts,&QTimer::timeout,&app,[&]{if(interrupted&&!terminal&&service.running()){requestedExit=interruptExit();service.cancel();}});
    std::signal(SIGINT,interruptHandler);std::signal(SIGTERM,interruptHandler);
#ifdef SIGPIPE
    std::signal(SIGPIPE,SIG_IGN);
#endif
#ifdef SIGBREAK
    std::signal(SIGBREAK,interruptHandler);
#endif
    QTimer::singleShot(0,&app,[&]{
        try {
            if(interrupted){terminal=true;reporter.event("cancelled",{{"exit_code",interruptExit()},{"reason","interrupted"}});app.exit(interruptExit());return;}
            reporter.event("scan-launch",{{"output_directory",utf8(output)},{"planned_cases",plan.at("cases").size()},{"histories_per_case",loaded.project.events},{"total_planned_histories",plan.at("total_planned_histories")}});
            service.start(loaded.project,settings,output);if(seconds)timeout.start(seconds*1000);interrupts.start();
        }catch(const std::exception& error){terminal=true;reporter.error(QString::fromUtf8(error.what()),2);app.exit(2);}
    });
    return app.exec();
}
int inspect(const RuntimeOptions& runtime, Reporter& reporter) {
    const auto environment = AnalysisService::childEnvironment(runtime);
    const auto assets = QFileInfo(runtime.worker).absoluteDir();
    const auto workerHash = fingerprint(runtime.worker);
    const auto contextHash = fingerprint(assets.filePath("radiation-build.json"));
    const auto manifestHash = fingerprint(assets.filePath("radiation-datasets.txt"));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(runtime.worker);
    process.setArguments({"--inspect"});
    QByteArray output, errors;
    bool oversized = false;
    auto drain = [&] {
        const auto bytes = process.readAllStandardOutput();
        if (output.size() + bytes.size() > 2 * 1024 * 1024) { oversized = true; process.kill(); }
        else output += bytes;
        errors = (errors + process.readAllStandardError()).right(8192);
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, drain);
    QObject::connect(&process, &QProcess::readyReadStandardError, &process, drain);
    struct Cleanup {
        QProcess& process;
        ~Cleanup() {
            QObject::disconnect(&process, nullptr, &process, nullptr);
            if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(3000); }
        }
    } cleanup{process};
    process.start();
    if (!process.waitForStarted(5000)) return reporter.error("Cannot start Worker inspection: " + process.errorString(), 3);
    if (!process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000);
        return reporter.error("Worker inspection timed out", 3);
    }
    drain();
    if (oversized || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return reporter.error("Worker inspection failed: " + QString::fromUtf8(errors), 3);
    try {
        const auto audit = Json::parse(output.toStdString());
        if (audit.at("schema") != "scdat.radiation-runtime/1" || audit.at("status") != "ready" ||
                audit.at("manifest_sha256") != manifestHash.toStdString() ||
                audit.at("build_context_sha256") != contextHash.toStdString() ||
                fingerprint(runtime.worker) != workerHash ||
                fingerprint(assets.filePath("radiation-build.json")) != contextHash ||
                fingerprint(assets.filePath("radiation-datasets.txt")) != manifestHash)
            throw std::runtime_error("Runtime inspection identity mismatch");
        reporter.event("runtime", {{"exit_code", 0}, {"worker", utf8(runtime.worker)}, {"worker_sha256", workerHash.toStdString()}, {"audit", audit}});
        return 0;
    } catch (const std::exception& error) { return reporter.error(QString::fromUtf8(error.what()), 3); }
}
int analyze(QCoreApplication& app, const QCommandLineParser& parser, Reporter& reporter) {
    auto loaded = loadInput(parser);
    if (parser.value("output").trimmed().isEmpty()) throw std::runtime_error("A new output directory is required");
    const auto output = QFileInfo(parser.value("output")).absoluteFilePath();
    Json fluence=nullptr;
    if(parser.isSet("fluence-settings")){
        QFile file(parser.value("fluence-settings"));
        if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>65536)
            throw std::runtime_error("Choose a fluence settings JSON file <=64 KiB");
        const auto bytes=file.read(65537);
        if(bytes.size()>65536||!file.atEnd()||file.error()!=QFileDevice::NoError)
            throw std::runtime_error("Cannot read bounded fluence settings");
        const auto spec=engine::TargetFluenceSpec::fromJson(Json::parse(bytes.toStdString()));
        spec.volume(loaded.project);
        fluence=spec.toJson();
    }
    int seconds = 0;
    if (parser.isSet("timeout-seconds")) {
        bool ok = false;
        seconds = parser.value("timeout-seconds").toInt(&ok);
        if (!ok || seconds < 0 || seconds > 86400) throw std::runtime_error("Timeout must be 0..86400 seconds; 0 disables it");
    }
    const auto runtime = loadRuntimeFile(runtimePath(parser));
    AnalysisService::childEnvironment(runtime); // Validate before reporting a launch or creating output.
    AnalysisService service;
    service.configure(runtime);
    bool terminal = false;
    int requestedExit = 0;
    QTimer timeout, interrupts;
    timeout.setSingleShot(true);
    interrupts.setInterval(50);
    auto cancelled = [&] {
        if (terminal) return;
        terminal = true;
        const int code = requestedExit ? requestedExit : interruptExit();
        reporter.event("cancelled", {{"exit_code", code}, {"reason", code == 124 ? "timeout" : "interrupted"}, {"output_directory", utf8(output)}});
        app.exit(code);
    };
    QObject::connect(&service, &AnalysisService::stateChanged, &app, [&](const QString& state) {
        if (terminal) return;
        if (state == "cancelled") cancelled();
        else reporter.event("state", {{"state", utf8(state)}});
    });
    QObject::connect(&service, &AnalysisService::progress, &app, [&](int done, int total) {
        if (!terminal) reporter.event("progress", {{"completed", done}, {"total", total}});
    });
    QObject::connect(&service, &AnalysisService::logMessage, &app, [&](const QString& text) {
        if (parser.isSet("verbose")) std::cerr << utf8(text) << std::flush;
    });
    QObject::connect(&service, &AnalysisService::failed, &app, [&](const QString& message) {
        if (terminal) return;
        if (requestedExit || interrupted) { cancelled(); return; }
        terminal = true;
        reporter.error(message, 3);
        app.exit(3);
    });
    QObject::connect(&service, &AnalysisService::completed, &app, [&](const QByteArray& bytes) {
        if (terminal) return;
        try {
            const auto result = Json::parse(bytes.toStdString());
            Json summary = {{"exit_code", 0}, {"result_file", utf8(QDir(output).filePath("result.json"))},
                {"result_sha256", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString()},
                {"result_schema", result.at("schema")}, {"histories", result.at("histories")},
                {"engine_version", result.at("engine_version")}, {"expected_primaries", result.at("expected_primaries")},
                {"targets", result.at("targets")}, {"capabilities", result.at("capability_status")}};
            if(result.contains("target_fluence")) summary["target_fluence"]=result.at("target_fluence");
            if(result.contains("source_importance")) summary["source_importance"]=result.at("source_importance");
            if(result.contains("source_importance")) summary["importance_review"]=importanceReview(Project::fromJson(result.at("project")),result);
            terminal = true;
            reporter.event("completed", std::move(summary));
            app.exit(0);
        } catch (const std::exception& error) {
            terminal = true;
            reporter.error("Unsupported completion payload: " + QString::fromUtf8(error.what()), 3);
            app.exit(3);
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &app, [&] {
        if (!terminal && service.running()) { requestedExit = 124; service.cancel(); }
    });
    QObject::connect(&interrupts, &QTimer::timeout, &app, [&] {
        if (interrupted && !terminal && service.running()) { requestedExit = interruptExit(); service.cancel(); }
    });
    std::signal(SIGINT, interruptHandler);
    std::signal(SIGTERM, interruptHandler);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
#ifdef SIGBREAK
    std::signal(SIGBREAK, interruptHandler);
#endif
    QTimer::singleShot(0, &app, [&] {
        try {
            if (interrupted) { cancelled(); return; }
            reporter.event("launch", {{"source_project", utf8(QFileInfo(parser.value("project")).absoluteFilePath())},
                {"source_sha256", loaded.project.provenance.at("last_loaded_input").at("sha256")},
                {"output_directory", utf8(output)}, {"histories", loaded.project.events}});
            service.start(loaded.project, output,fluence);
            if (seconds) timeout.start(seconds * 1000);
            interrupts.start();
        } catch (const std::exception& error) {
            terminal = true;
            reporter.error(QString::fromUtf8(error.what()), 2);
            app.exit(2);
        }
    });
    return app.exec();
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("spacerad_cli");
    app.setApplicationVersion(SPACERAD_APP_VERSION);
    Reporter reporter;
    QCommandLineParser parser;
    parser.setApplicationDescription("SpaceRad project validation and shared-service analysis. Machine commands emit NDJSON.");
    const auto help = parser.addHelpOption();
    const auto version = parser.addVersionOption();
    parser.addPositionalArgument("command", "validate | inspect-runtime | run | inspect-result | compare | plan-scan | scan | inspect-scan | fold-response | inspect-response | fold-local-response");
    parser.addOptions({{{"p", "project"}, "Input project JSON.", "file"},
        {{"r", "runtime"}, "Runtime JSON; default: runtime.json beside the executable.", "file"},
        {{"o", "output"}, "New output directory (run, scan or fold-response).", "directory"},
        {"response", "Device response CSV (fold-response or fold-local-response).", "file"},
        {"fluence-settings", "Optional target fluence settings JSON (run only).", "file"},
        {"scan-settings", "Shielding scan settings JSON (plan-scan or scan).", "file"},
        {"job", "Completed archive directory (inspect-result, inspect-scan or inspect-response).", "directory"},
        {"baseline", "Baseline completed job directory (compare only).", "directory"},
        {"candidate", "Candidate completed job directory (compare only).", "directory"},
        {"histories", "Override histories (2..100000000).", "count"},
        {"timeout-seconds", "Cancel run after N seconds; 0 means unlimited.", "seconds"},
        {"verbose", "Forward Worker text to stderr; stdout remains NDJSON."}});
    if (!parser.parse(app.arguments())) return reporter.error(parser.errorText(), 2);
    if (parser.isSet(help)) { std::cout << utf8(parser.helpText()); return 0; }
    if (parser.isSet(version)) { std::cout << "spacerad_cli " << SPACERAD_APP_VERSION << '\n'; return 0; }
    try {
        const auto positional = parser.positionalArguments();
        if (positional.size() != 1 || !QStringList{"validate", "inspect-runtime", "run", "inspect-result", "compare", "plan-scan", "scan", "inspect-scan", "fold-response", "inspect-response", "fold-local-response"}.contains(positional[0]))
            throw std::runtime_error("Choose exactly one command; use --help for supported commands");
        const auto command = positional[0];
        for (const auto& option : parser.optionNames()) {
            QString canonical = option == "p" ? "project" : option == "r" ? "runtime" : option == "o" ? "output" : option;
            const bool allowed = (command == "run" && QStringList{"project", "runtime", "output", "histories", "timeout-seconds", "verbose","fluence-settings"}.contains(canonical)) ||
                (command == "validate" && (canonical == "project" || canonical == "histories")) ||
                (command == "inspect-runtime" && canonical == "runtime") || (command == "inspect-result" && canonical == "job") ||
                (command == "compare" && (canonical == "baseline" || canonical == "candidate")) ||
                (command == "plan-scan" && QStringList{"project","histories","scan-settings"}.contains(canonical)) ||
                (command == "scan" && QStringList{"project","histories","scan-settings","runtime","output","timeout-seconds","verbose"}.contains(canonical)) ||
                ((command == "inspect-scan" || command == "inspect-response") && canonical == "job") ||
                (command == "fold-response" && QStringList{"project","response","output"}.contains(canonical)) ||
                (command == "fold-local-response" && (canonical == "job" || canonical == "response"));
            if (!allowed) throw std::runtime_error("Option is not supported by this command");
            if (parser.values(canonical).size() > 1) throw std::runtime_error("Duplicate option value");
        }
        if (command == "run") return analyze(app, parser, reporter);
        if (command == "scan") return scan(app, parser, reporter);
        if (command == "plan-scan") {const auto input=loadInput(parser);reporter.event("scan-planned",{{"exit_code",0},{"plan",planShieldingScan(input.project,scanSettings(parser))}});return 0;}
        if (command == "inspect-scan") {reporter.event("archived-scan",{{"exit_code",0},{"report",loadShieldingScan(parser.value("job"))}});return 0;}
        if (command == "fold-local-response") {
            const auto run=loadArchivedRun(parser.value("job"));
            const auto table=loadDeviceResponseCsv(parser.value("response"));
            reporter.event("local-response-folded",{{"exit_code",0},{"report",foldTargetFluenceResponse(run,table)}});
            return 0;
        }
        if (command == "fold-response") {
            if (parser.value("response").trimmed().isEmpty() || parser.value("output").trimmed().isEmpty())
                throw std::runtime_error("A response CSV and a new output directory are required");
            const auto input=loadInput(parser);
            const auto output=QFileInfo(parser.value("output")).absoluteFilePath();
            const auto report=saveDeviceResponseStudy(input.project,parser.value("response"),output);
            reporter.event("response-folded",{{"exit_code",0},{"output_directory",utf8(output)},{"report",report}});
            return 0;
        }
        if (command == "inspect-response") {
            reporter.event("archived-response",{{"exit_code",0},{"report",loadDeviceResponseStudy(parser.value("job"))}});
            return 0;
        }
        if (command == "inspect-runtime") return inspect(loadRuntimeFile(runtimePath(parser)), reporter);
        if (command == "inspect-result") {
            const auto run = loadArchivedRun(parser.value("job"));
            reporter.event("archived-result", {{"exit_code", 0}, {"summary", run.summary()}});
            return 0;
        }
        if (command == "compare") {
            const auto a = loadArchivedRun(parser.value("baseline"));
            const auto b = loadArchivedRun(parser.value("candidate"));
            reporter.event("comparison", {{"exit_code", 0}, {"comparison", compareRuns(a, b)}});
            return 0;
        }
        const auto loaded = loadInput(parser);
        const auto compiled = engine::compileGeometry(loaded.project);
        Json targets = Json::array();
        for (const auto& volume : compiled.volumes) if (volume.sensitive) targets.push_back(volume.id);
        reporter.event("validated", {{"exit_code", 0}, {"project", utf8(QFileInfo(parser.value("project")).absoluteFilePath())},
            {"input_schema", loaded.inputSchema}, {"source_sha256", loaded.project.provenance.at("last_loaded_input").at("sha256")},
            {"name", loaded.project.name}, {"histories", loaded.project.events}, {"sensitive_ids", targets},
            {"expected_primaries", loaded.project.expectedPrimaries()}, {"synthetic", loaded.project.synthetic},
            {"spectrum_quantity", engine::quantityName(loaded.project.quantity)}});
        return 0;
    } catch (const std::exception& error) { return reporter.error(QString::fromUtf8(error.what()), 2); }
}
