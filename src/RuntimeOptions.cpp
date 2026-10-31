#include "spacerad/AnalysisService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <set>
#include <stdexcept>

namespace spacerad {
RuntimeOptions loadRuntimeFile(const QString& path) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        throw std::runtime_error("Cannot read runtime configuration <=64 KiB");
    const auto bytes = file.read(65537);
    if (file.error() != QFileDevice::NoError || bytes.size() > 65536)
        throw std::runtime_error("Cannot read runtime configuration <=64 KiB");
    std::vector<std::set<std::string>> keys;
    const auto json = Json::parse(bytes.toStdString(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 16) throw std::runtime_error("Runtime JSON nesting limit exceeded");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key) {
            if (!keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate runtime JSON field");
        } else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
    if (!json.is_object() || json.size() != 4 || json.at("schema") != "spacerad.runtime/1" ||
            !json.at("library_directories").is_array())
        throw std::runtime_error("Unsupported runtime configuration");
    const auto base = QFileInfo(path).absoluteDir();
    const auto resolve = [&](const Json& value) {
        const auto text = QString::fromStdString(value.get<std::string>());
        if (text.trimmed().isEmpty()) throw std::runtime_error("Empty runtime path");
        return QDir::cleanPath(base.absoluteFilePath(text));
    };
    RuntimeOptions result;
    result.worker = resolve(json.at("worker"));
    result.dataRoot = resolve(json.at("data_root"));
    for (const auto& value : json.at("library_directories")) result.libraryDirectories << resolve(value);
    return result;
}
}
