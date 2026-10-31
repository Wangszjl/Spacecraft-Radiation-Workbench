#include <spacerad/AnalysisService.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <functional>
#include <iostream>
#include <stdexcept>

using spacerad::RuntimeOptions;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int checks = 0;
    const auto check = [&](bool ok, const char* why) {
        ++checks;
        if (!ok) throw std::runtime_error(why);
    };
    const auto rejects = [](const std::function<void()>& operation) {
        try { operation(); return false; }
        catch (const std::exception&) { return true; }
    };

    try {
        QTemporaryDir temp;
        check(temp.isValid(), "temporary directory created");
        const QString root = temp.path() + QStringLiteral("/目录 with spaces");
        check(QDir().mkpath(root + QStringLiteral("/configs")), "Unicode config directory created");
        const QString config = root + QStringLiteral("/configs/runtime.json");

        const auto write = [](const QString& path, const QByteArray& bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                throw std::runtime_error("could not create runtime fixture");
            if (file.write(bytes) != bytes.size()) throw std::runtime_error("could not write runtime fixture");
        };
        const auto valid = [](const QByteArray& worker = "../bin/worker", const QByteArray& data = "../data",
                              const QByteArray& libraries = "[\"../lib\", \"plugins\"]") {
            return QByteArray("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"") + worker +
                "\",\"data_root\":\"" + data + "\",\"library_directories\":" + libraries + "}";
        };

        write(config, valid());
        const RuntimeOptions options = spacerad::loadRuntimeFile(config);
        const QString base = QDir::cleanPath(root + QStringLiteral("/configs"));
        check(options.worker == QDir::cleanPath(base + QStringLiteral("/../bin/worker")), "worker resolved against config directory");
        check(options.dataRoot == QDir::cleanPath(base + QStringLiteral("/../data")), "data root resolved against config directory");
        check(options.libraryDirectories.size() == 2, "library directory entries loaded");
        check(options.libraryDirectories.at(0) == QDir::cleanPath(base + QStringLiteral("/../lib")), "relative library path resolved");
        check(options.libraryDirectories.at(1) == QDir::cleanPath(base + QStringLiteral("/plugins")), "second relative library path resolved");
        check(QDir::isAbsolutePath(options.worker) && QDir::isAbsolutePath(options.dataRoot), "runtime paths are absolute");
        check(options.worker.contains(QStringLiteral("目录 with spaces")), "Unicode and spaces preserved in paths");

        const QString oldCwd = QDir::currentPath();
        check(QDir::setCurrent(temp.path()), "can switch to unrelated current directory");
        const RuntimeOptions fromOtherCwd = spacerad::loadRuntimeFile(config);
        check(fromOtherCwd.worker == options.worker && fromOtherCwd.dataRoot == options.dataRoot,
              "relative paths do not depend on current working directory");
        check(QDir::setCurrent(oldCwd), "original current directory restored");

        write(config, valid("./bin/../worker", "./data/../root", "[\"./lib/../modules\"]"));
        const RuntimeOptions cleaned = spacerad::loadRuntimeFile(config);
        check(cleaned.worker == QDir::cleanPath(base + QStringLiteral("/worker")), "worker path cleaned");
        check(cleaned.dataRoot == QDir::cleanPath(base + QStringLiteral("/root")), "data path cleaned");
        check(cleaned.libraryDirectories.at(0) == QDir::cleanPath(base + QStringLiteral("/modules")), "library path cleaned");

        write(config, valid("absolute-worker", "absolute-data", "[]"));
        const RuntimeOptions noLibraries = spacerad::loadRuntimeFile(config);
        check(noLibraries.libraryDirectories.isEmpty(), "empty library array accepted");
        check(noLibraries.worker == QDir::cleanPath(base + QStringLiteral("/absolute-worker")), "worker populated for empty library case");

        const auto mustReject = [&](const QByteArray& bytes, const char* reason) {
            write(config, bytes);
            check(rejects([&] { (void)spacerad::loadRuntimeFile(config); }), reason);
        };
        mustReject(valid().replace("spacerad.runtime/1", "spacerad.runtime/2"), "wrong schema rejected");
        mustReject(valid().replace("\"schema\":\"spacerad.runtime/1\"", "\"schema\":17"), "non-string schema rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"data_root\":\"d\",\"library_directories\":[]}", "missing worker rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"library_directories\":[]}", "missing data root rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":\"d\"}", "missing library list rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":\"d\",\"library_directories\":[],\"unexpected\":true}", "unknown top level field rejected");
        mustReject(valid("", "d", "[]"), "empty worker path rejected");
        mustReject(valid("w", "  ", "[]"), "whitespace-only data path rejected");
        mustReject(valid("w", "d", "[\"\"]"), "empty library path rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":17,\"data_root\":\"d\",\"library_directories\":[]}", "non-string worker rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":false,\"library_directories\":[]}", "non-string data root rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":\"d\",\"library_directories\":\"lib\"}", "non-array library list rejected");
        mustReject(valid("w", "d", "[\"ok\", 9]"), "non-string library item rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":\"d\",\"library_directories\":[]}", "duplicate JSON field rejected");
        mustReject("{\"schema\":\"spacerad.runtime/1\",\"worker\":\"w\",\"data_root\":\"d\",\"library_directories\":[],}", "malformed JSON rejected");
        mustReject(QByteArray(65537, ' '), "configuration above 65536 bytes rejected");
        QByteArray tooDeep = valid("w", "d", "[]");
        tooDeep.replace("[]", QByteArray("[") + QByteArray(17, '[') + "0" + QByteArray(17, ']') + "]");
        mustReject(tooDeep, "nesting deeper than 16 rejected");
        const QString missing = root + QStringLiteral("/does-not-exist.json");
        check(rejects([&] { (void)spacerad::loadRuntimeFile(missing); }), "unopenable runtime file rejected");

        write(config, QByteArray::fromHex("efbbbf") + valid());
        const RuntimeOptions bom = spacerad::loadRuntimeFile(config);
        check(bom.worker == QDir::cleanPath(base + QStringLiteral("/../bin/worker")), "UTF-8 BOM accepted");
        const auto beforeEnvironment = QProcessEnvironment::systemEnvironment();
        write(config, valid("env-worker", "env-data", "[]"));
        (void)spacerad::loadRuntimeFile(config);
        const auto afterEnvironment = QProcessEnvironment::systemEnvironment();
        check(beforeEnvironment.toStringList() == afterEnvironment.toStringList(), "loading runtime leaves process environment unchanged");

        std::cout << checks << " runtime option checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
