// Test-only protocol fault injection. No transport is executed by this binary.
#include "spacerad/Project.h"
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    const auto args = app.arguments();
    auto digest = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing fault-test asset");
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
    };
    try {
        const auto loaded = spacerad::engine::loadProjectFile(args[1]);
        const auto directory = QCoreApplication::applicationDirPath();
        spacerad::Json result = {{"schema", "scdat.radiation-result/2"}, {"status", "completed"},
            {"histories", loaded.project.events}, {"project", loaded.project.toJson()},
            {"project_sha256", digest(args[1])}, {"worker", {{"sha256", digest(QCoreApplication::applicationFilePath())}}},
            {"runtime_audit", {{"status", "ready"}, {"manifest_sha256", digest(directory + "/radiation-datasets.txt")},
                {"build_context_sha256", digest(directory + "/radiation-build.json")}}},
            {"targets", spacerad::Json::array()}};
        const auto geometry = spacerad::engine::compileGeometry(loaded.project);
        for (const auto& volume : geometry.volumes) if (volume.sensitive)
            result["targets"].push_back({{"id", volume.id}, {"mass_kg", 1}, {"tid_Gy", 0}, {"tid_standard_error_Gy", 0}});
        // Omit mandatory CLI completion metadata deliberately.
        QFile output(args[2]);
        const auto bytes = result.dump();
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(bytes.data(), qint64(bytes.size())) != qint64(bytes.size())) return 3;
        return 0;
    } catch (...) { return 3; }
}
