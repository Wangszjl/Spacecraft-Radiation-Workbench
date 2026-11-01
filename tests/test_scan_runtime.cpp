#include "spacerad/ShieldingScan.h"
#include "spacerad/Environment.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace spacerad;
Json summary = Json::array();
int failures = 0;

void check(bool ok, const std::string& name, Json actual = nullptr) {
    std::cout << (ok ? "PASS " : "FAIL ") << name;
    if (!actual.is_null()) std::cout << " actual=" << actual.dump();
    std::cout << std::endl;
    summary.push_back({{"check", name}, {"passed", ok}, {"actual", actual}});
    if (!ok) ++failures;
}

struct WaitResult { bool timedOut = false; QString error; };

WaitResult waitForTerminal(ShieldingScanService& service, int timeoutMs = 60000) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    WaitResult result;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { result.timedOut = true; loop.quit(); });
    QObject::connect(&service, &ShieldingScanService::completed, &loop, [&] { loop.quit(); });
    QObject::connect(&service, &ShieldingScanService::failed, &loop,
                     [&](const QString& error) { result.error = error; loop.quit(); });
    QObject::connect(&service, &ShieldingScanService::stateChanged, &loop,
                     [&](const QString& state) { if (state == "cancelled") loop.quit(); });
    timeout.start(timeoutMs);
    loop.exec();
    return result;
}

Project fixture(int histories) {
    Project p = Project::demo();
    p.name = "Shielding scan runtime contract";
    p.radius = 30;
    p.events = histories;
    p.seed = 24681357;
    p.volumes = {
        Volume{"Spacecraft_Al", "G4_Al", {0,0,0}, {30,30,30}, .5, false},
        Volume{"Target_Si", "G4_Si", {0,0,0}, {20,20,20}, 0, true}
    };
    p.volumes[1].shape = "sphere";
    p.validate();
    return setManualSpectrum(p, p.spectra.size(), "e-", {1, 3}, {2, 4});
}

bool copyTree(const QString& source, const QString& destination) {
    if (!QDir().mkpath(destination)) return false;
    QDirIterator it(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString relative = QDir(source).relativeFilePath(from);
        const QString to = QDir(destination).filePath(relative);
        if (QFileInfo(from).isDir()) {
            if (!QDir().mkpath(to)) return false;
        } else {
            if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to)) return false;
        }
    }
    return true;
}

void writeSummary(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("Cannot write regression summary JSON");
    const auto raw = Json{{"passed",failures==0},{"checks",summary}}.dump(2);
    if (file.write(raw.data(), static_cast<qint64>(raw.size())) != static_cast<qint64>(raw.size()))
        throw std::runtime_error("Cannot finish regression summary JSON");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) {
        std::cerr << "Usage: test_scan_runtime <runtime.json> <new-output-directory>" << std::endl;
        return 2;
    }
    const QString runtimePath = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString output = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    const QString summaryPath = output + "/summary.json";
    try {
        const RuntimeOptions runtime = loadRuntimeFile(runtimePath);
        Project project = fixture(2000);
        const Json original = project.toJson();
        const ShieldingScanSettings settings{"Spacecraft_Al", {0, .5, 2}};
        QTemporaryDir temporary;
        if (!temporary.isValid()) throw std::runtime_error("Cannot create temporary test directory");

        ShieldingScanService service;
        service.configure(runtime);
        int completedCases = 0, totalCases = 0, intermediateProgress = 0;
        int completedHistories = 0, totalHistories = 0, completedSignals = 0, failedSignals = 0;
        QString failureMessage;
        QObject::connect(&service, &ShieldingScanService::progress, &service,
            [&](int done, int total, int histories, int historiesTotal) {
                completedCases = done; totalCases = total;
                if (done > 0 && done < total) ++intermediateProgress;
                if (historiesTotal > 0) {
                    completedHistories = std::max(completedHistories, histories);
                    totalHistories = historiesTotal;
                }
            });
        QObject::connect(&service, &ShieldingScanService::completed, &service,
                         [&](const QByteArray&) { ++completedSignals; });
        QObject::connect(&service, &ShieldingScanService::failed, &service,
                         [&](const QString& e) { ++failedSignals; failureMessage = e; });
        service.start(project, settings, output);

        const QString busyDirectory = temporary.filePath("busy-start-must-not-create");
        bool busyStartRejected = false;
        try { service.start(project, settings, busyDirectory); } catch (const std::exception&) { busyStartRejected = true; }
        bool configureRejected = false;
        try { service.configure(runtime); } catch (const std::exception&) { configureRejected = true; }
        check(busyStartRejected && !QFileInfo::exists(busyDirectory),
              "busy scan rejects a second start before making another directory", busyStartRejected);
        check(configureRejected, "busy scan rejects runtime reconfiguration", configureRejected);

        const WaitResult firstWait = waitForTerminal(service);
        const bool finished = !firstWait.timedOut && service.state() == "completed" && !service.result().isEmpty();
        check(finished, "real Worker scan completes", Json{{"state", service.state().toStdString()},
              {"timed_out", firstWait.timedOut}, {"error", firstWait.error.toStdString()},
              {"signal_error", failureMessage.toStdString()}});
        Json accepted;
        if (finished) accepted = Json::parse(service.result().toStdString());
        const int caseCount = finished ? static_cast<int>(accepted.at("cases").size()) : -1;
        const int curveCount = finished ? static_cast<int>(accepted.at("curves").size()) : -1;
        check(finished && caseCount == 6 && completedCases == 6 && totalCases == 6,
              "two species across three thicknesses produce six completed cases",
              Json{{"report_cases", caseCount}, {"progress_completed", completedCases}, {"progress_total", totalCases}});
        check(finished && curveCount == 3, "completed scan has one curve row per thickness", curveCount);
        check(intermediateProgress > 0, "scan progress includes an intermediate case count", intermediateProgress);
        check(completedSignals == 1 && failedSignals == 0,
              "completion emits one completed signal and no failure signal",
              Json{{"completed", completedSignals}, {"failed", failedSignals}});
        check(project.toJson() == original, "Worker scan leaves the caller project unchanged");

        Json loaded;
        bool archiveLoaded = false;
        QString archiveError;
        try { loaded = loadShieldingScan(output); archiveLoaded = true; }
        catch (const std::exception& e) { archiveError = QString::fromUtf8(e.what()); }
        check(archiveLoaded && loaded == accepted, "offline loader accepts the completed scan archive", archiveError.toStdString());

        const QString corruptCopy = temporary.filePath("corrupt-copy");
        bool copied = archiveLoaded && copyTree(output, corruptCopy);
        bool tampered = false;
        if (copied) {
            QFile file(corruptCopy + "/scan-result.json");
            if (file.open(QIODevice::Append)) tampered = file.write(" ") == 1;
        }
        bool tamperRejected = false;
        QString tamperError;
        if (tampered) try { (void)loadShieldingScan(corruptCopy); }
            catch (const std::exception& e) { tamperRejected = true; tamperError = QString::fromUtf8(e.what()); }
        check(tamperRejected, "offline loader rejects result-byte tampering in an isolated copy",
              Json{{"copy_ready", copied}, {"tampered", tampered}, {"rejected", tamperRejected}, {"error", tamperError.toStdString()}});

        ShieldingScanService immediateCancel;
        immediateCancel.configure(runtime);
        int immediateCompleted = 0;
        QObject::connect(&immediateCancel, &ShieldingScanService::completed, &immediateCancel,
                         [&](const QByteArray&) { ++immediateCompleted; });
        QObject::connect(&immediateCancel, &ShieldingScanService::stateChanged, &immediateCancel,
                         [&](const QString& state) { if (state == "running") immediateCancel.cancel(); });
        immediateCancel.start(fixture(100000000), settings, temporary.filePath("signal-cancel"));
        check(immediateCancel.state() == "cancelled" && !immediateCancel.running() &&
              immediateCancel.result().isEmpty() && immediateCompleted == 0,
              "cancellation from initial running state signal prevents acceptance/launch",
              Json{{"state", immediateCancel.state().toStdString()}, {"completed", immediateCompleted}});

        ShieldingScanService activeCancel;
        activeCancel.configure(runtime);
        const QString activeDirectory = temporary.filePath("active-cancel");
        int cancelCompleted = 0;
        QObject::connect(&activeCancel, &ShieldingScanService::completed, &activeCancel,
                         [&](const QByteArray&) { ++cancelCompleted; });
        QObject::connect(&activeCancel,&ShieldingScanService::stateChanged,&activeCancel,[&](const QString& state){if(state=="cancelling")activeCancel.cancel();});
        activeCancel.start(fixture(100000000), settings, activeDirectory);
        QTimer::singleShot(50, &activeCancel, [&] { activeCancel.cancel(); });
        const WaitResult cancelWait = waitForTerminal(activeCancel);
        check(!cancelWait.timedOut && activeCancel.state() == "cancelled" && activeCancel.result().isEmpty() && cancelCompleted == 0,
              "active 100M-history scan cancels without accepting a result",
              Json{{"state", activeCancel.state().toStdString()}, {"timed_out", cancelWait.timedOut},
                   {"completed", cancelCompleted}, {"error", cancelWait.error.toStdString()}});
        check(loadShieldingScan(activeDirectory).at("status")=="cancelled","cancelled partial scan remains an inspectable terminal archive");

        const auto copiedRuntimeDirectory=temporary.filePath("runtime");QDir().mkpath(copiedRuntimeDirectory);
        auto copiedRuntime=runtime;copiedRuntime.worker=copiedRuntimeDirectory+"/"+QFileInfo(runtime.worker).fileName();
        for(const auto& name:{QFileInfo(runtime.worker).fileName(),QString("radiation-build.json"),QString("radiation-datasets.txt")})
            if(!QFile::copy(QFileInfo(runtime.worker).dir().filePath(name),copiedRuntimeDirectory+"/"+name))throw std::runtime_error("Cannot copy test runtime assets");
        ShieldingScanService changedRuntime;changedRuntime.configure(copiedRuntime);bool changed=false;
        QObject::connect(&changedRuntime,&ShieldingScanService::progress,&changedRuntime,[&](int done,int,int,int){if(done==1&&!changed){QFile f(copiedRuntimeDirectory+"/radiation-build.json");if(!f.open(QIODevice::Append)||f.write(" ")!=1)throw std::runtime_error("Cannot alter copied runtime");changed=true;}});
        const auto changedDirectory=temporary.filePath("changed-runtime");changedRuntime.start(project,settings,changedDirectory);
        const auto changedWait=waitForTerminal(changedRuntime);
        check(!changedWait.timedOut && changed && changedRuntime.state()=="failed" && changedRuntime.result().isEmpty() && !QFileInfo::exists(changedDirectory+"/cases/case-000001"),"runtime asset change between cases prevents the next Worker launch");
        check(loadShieldingScan(changedDirectory).at("status")=="failed","failed scan retains verified completed children only");
        const auto deletedDirectory=temporary.filePath("delete-on-running");
        QPointer<ShieldingScanService> deleted=new ShieldingScanService;deleted->configure(runtime);
        QObject::connect(deleted,&ShieldingScanService::stateChanged,&app,[&](const QString& state){if(state=="running")delete deleted.data();});
        deleted->start(project,settings,deletedDirectory);
        check(deleted.isNull()&&loadShieldingScan(deletedDirectory).at("status")=="cancelled","host may destroy scan service from initial state notification");
        QPointer<ShieldingScanService> deletedAtCompletion=new ShieldingScanService;deletedAtCompletion->configure(runtime);
        QEventLoop deletionLoop;QTimer deletionTimeout;deletionTimeout.setSingleShot(true);QObject::connect(&deletionTimeout,&QTimer::timeout,&deletionLoop,&QEventLoop::quit);deletionTimeout.start(60000);
        QObject::connect(deletedAtCompletion,&ShieldingScanService::completed,&deletionLoop,[&](const QByteArray&){delete deletedAtCompletion.data();deletionLoop.quit();});
        deletedAtCompletion->start(project,settings,temporary.filePath("delete-on-completed"));deletionLoop.exec();
        check(deletedAtCompletion.isNull(),"host may destroy scan service from completion notification");
        if(deletedAtCompletion)delete deletedAtCompletion.data();

        check(totalHistories > 0 && completedHistories >= 0,
              "history progress reports actual nonzero planned history total",
              Json{{"last_completed_histories", completedHistories}, {"total_histories", totalHistories}});
        check(failures == 0, "all accumulated checks pass", failures);
        writeSummary(summaryPath);
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        summary.push_back({{"check", "uncaught test exception"}, {"passed", false}, {"actual", e.what()}});
        ++failures;
        std::cerr << "ERROR " << e.what() << std::endl;
        try { writeSummary(summaryPath); } catch (...) {}
        return 1;
    }
}
