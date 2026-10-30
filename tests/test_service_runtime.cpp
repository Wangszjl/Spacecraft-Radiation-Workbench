#include "spacerad/AnalysisService.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using spacerad::AnalysisService;
using spacerad::Project;
using spacerad::RuntimeOptions;
using Json = spacerad::Json;

int failures = 0;

void check(bool condition, const char* name)
{
    std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
    if (!condition) ++failures;
}

bool waitForTerminal(AnalysisService& service, QString* failureMessage = nullptr)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    QObject::connect(&service, &AnalysisService::completed, &loop,
                     [&loop](const QByteArray&) { loop.quit(); });
    QObject::connect(&service, &AnalysisService::failed, &loop,
                     [&loop, failureMessage](const QString& message) {
                         if (failureMessage) *failureMessage = message;
                         loop.quit();
                     });
    QObject::connect(&service, &AnalysisService::stateChanged, &loop,
                     [&loop](const QString& state) {
                         if (state == QStringLiteral("cancelled")) loop.quit();
                     });

    timeout.start(60000);
    loop.exec();
    return !timedOut;
}

RuntimeOptions runtimeFromArguments(int argc, char** argv)
{
    RuntimeOptions options;
    options.worker = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    options.dataRoot = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    for (int i = 3; i < argc; ++i)
        options.libraryDirectories.push_back(
            QFileInfo(QString::fromLocal8Bit(argv[i])).absoluteFilePath());
    return options;
}

Project testProject(int histories)
{
    Project project = Project::demo();
    project.events = histories;
    project.seed = 24681357;
    return project;
}

bool startAndWait(AnalysisService& service, const Project& project,
                  const QString& directory, Json* result, QString* error = nullptr)
{
    try {
        service.start(project, directory);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    const bool finished = waitForTerminal(service, error);
    if (finished && service.state() == QStringLiteral("completed") && !service.result().isEmpty()) {
        if (result) *result = Json::parse(service.result().toStdString());
        return true;
    }
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::cerr << "Usage: test_service_runtime <worker> <dataRoot> <libraryDir> [libraryDir ...]"
                  << std::endl;
        return 2;
    }

    const QByteArray pathBefore = qgetenv("PATH");
    const QByteArray g4ledataBefore = qgetenv("G4LEDATA");
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary output root is available");
    if (!temporary.isValid()) return 1;

    const RuntimeOptions options = runtimeFromArguments(argc, argv);
    AnalysisService service;

    const QString missingRuntimeDirectory = temporary.filePath(QStringLiteral("missing-runtime"));
    bool missingRuntimeRejected = false;
    try {
        service.start(testProject(2000), missingRuntimeDirectory);
    } catch (const std::exception&) {
        missingRuntimeRejected = true;
    }
    check(missingRuntimeRejected && !QFileInfo::exists(missingRuntimeDirectory),
          "missing runtime is rejected without creating the output directory");

    try {
        service.configure(options);
        check(true, "runtime options configure successfully");
    } catch (const std::exception& e) {
        std::cout << "FAIL runtime options configure successfully: " << e.what() << std::endl;
        ++failures;
        return 1;
    }

    const QString existingDirectory = temporary.filePath(QStringLiteral("already-exists"));
    if (QDir().mkpath(existingDirectory)) {
        QFile sentinel(existingDirectory + QStringLiteral("/sentinel"));
        const bool sentinelCreated = sentinel.open(QIODevice::WriteOnly) && sentinel.write("keep") == 4;
        sentinel.close();
        bool existingRejected = false;
        try {
            service.start(testProject(2000), existingDirectory);
        } catch (const std::exception&) {
            existingRejected = true;
        }
        check(sentinelCreated && existingRejected && QFileInfo::exists(existingDirectory + QStringLiteral("/sentinel")),
              "existing output directory is protected");
    } else {
        check(false, "existing output directory is protected (could not create fixture)");
    }

    Json firstResult;
    QString firstError;
    const bool firstCompleted = startAndWait(service, testProject(2000),
        temporary.filePath(QStringLiteral("repeat-1")), &firstResult, &firstError);
    check(firstCompleted, "first 2k-history Worker run completes and is accepted");
    if (!firstCompleted && !firstError.isEmpty())
        std::cout << "  Worker/service detail: " << firstError.toStdString() << std::endl;

    Json secondResult;
    QString secondError;
    const bool secondCompleted = startAndWait(service, testProject(2000),
        temporary.filePath(QStringLiteral("repeat-2")), &secondResult, &secondError);
    check(secondCompleted, "second 2k-history Worker run completes and is accepted");
    if (!secondCompleted && !secondError.isEmpty())
        std::cout << "  Worker/service detail: " << secondError.toStdString() << std::endl;

    if (firstCompleted && secondCompleted) {
        check(firstResult.at("targets") == secondResult.at("targets"),
              "fixed seed produces identical target doses and uncertainty values");
        check(firstResult.at("score_moments") == secondResult.at("score_moments"),
              "fixed seed produces identical Monte Carlo score moments");
    } else {
        check(false, "fixed seed produces identical target doses and uncertainty values");
        check(false, "fixed seed produces identical Monte Carlo score moments");
    }

    int progressSignals = 0;
    QObject::connect(&service, &AnalysisService::progress, &service,
                     [&progressSignals](int completed, int total) { if(completed>0&&completed<total)++progressSignals; });
    Json progressResult;
    QString progressError;
    const bool progressRun = startAndWait(service, testProject(2000),
        temporary.filePath(QStringLiteral("progress-run")), &progressResult, &progressError);
    check(progressRun && progressSignals > 0, "real Worker run emits at least one progress signal");

    const Project longProject = testProject(1000000);
    const QString longDirectory = temporary.filePath(QStringLiteral("cancel-run"));
    bool longRunStarted = false;
    try {
        service.start(longProject, longDirectory);
        longRunStarted = service.running();
    } catch (const std::exception& e) {
        std::cout << "FAIL million-history run starts for busy/cancel checks: " << e.what() << std::endl;
        ++failures;
    }
    check(longRunStarted, "million-history run starts for busy and cancellation checks");
    if (longRunStarted) {
        bool configureRejected = false;
        try {
            service.configure(options);
        } catch (const std::exception&) {
            configureRejected = true;
        }
        check(configureRejected, "configure is rejected while Worker is busy");

        const QString busyOutput = temporary.filePath(QStringLiteral("busy-start-must-not-create"));
        bool secondStartRejected = false;
        try {
            service.start(testProject(2000), busyOutput);
        } catch (const std::exception&) {
            secondStartRejected = true;
        }
        check(secondStartRejected && !QFileInfo::exists(busyOutput),
              "start is rejected while busy without creating another job directory");

        service.cancel();
        const bool cancelFinished = waitForTerminal(service);
        check(cancelFinished && service.state() == QStringLiteral("cancelled") && service.result().isEmpty(),
              "cancelling the million-history run does not accept a result");
    } else {
        check(false, "configure is rejected while Worker is busy");
        check(false, "start is rejected while busy without creating another job directory");
        check(false, "cancelling the million-history run does not accept a result");
    }

    const QString tamperedDirectory = temporary.filePath(QStringLiteral("tampered-input"));
    bool tamperedRunStarted = false;
    try {
        service.start(testProject(1000000), tamperedDirectory);
        tamperedRunStarted = service.running();
    } catch (const std::exception& e) {
        std::cout << "FAIL identity-check run starts: " << e.what() << std::endl;
        ++failures;
    }
    bool inputTampered = false;
    if (tamperedRunStarted) {
        QFile input(tamperedDirectory + QStringLiteral("/project.json"));
        if (input.open(QIODevice::ReadOnly)) {
            Json changed = Json::parse(input.readAll().toStdString());
            input.close();
            changed["name"] = "test input changed after launch";
            if (input.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                const std::string bytes = changed.dump(2);
                inputTampered = input.write(bytes.data(), static_cast<qint64>(bytes.size())) ==
                                static_cast<qint64>(bytes.size());
                input.close();
            }
        }
    }
    QString tamperError;
    const bool tamperedFinished = tamperedRunStarted && waitForTerminal(service, &tamperError);
    check(tamperedRunStarted && inputTampered && tamperedFinished &&
              service.state() == QStringLiteral("failed") && service.result().isEmpty(),
          "modified launch project identity is rejected without accepting a result");
    if (!tamperError.isEmpty())
        std::cout << "  Expected identity rejection: " << tamperError.toStdString() << std::endl;

    AnalysisService directCancel;
    directCancel.configure(options);
    QObject::connect(&directCancel,&AnalysisService::stateChanged,&directCancel,[&](const QString& state){if(state=="running")directCancel.cancel();});
    directCancel.start(testProject(1000000),temporary.filePath("direct-signal-cancel"));
    check(directCancel.state()=="cancelled"&&!directCancel.running()&&directCancel.result().isEmpty(),"direct running-signal cancellation does not launch a Worker");

    check(qgetenv("PATH") == pathBefore && qgetenv("G4LEDATA") == g4ledataBefore,
          "PATH and G4LEDATA remain unchanged in the host process");

    std::cout << "Checks complete: " << failures << " failure(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
