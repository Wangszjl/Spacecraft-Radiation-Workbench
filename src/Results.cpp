#include "spacerad/Results.h"
#include <spacerad/engine/SourceSampling.h>
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace spacerad {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QByteArray read(const QString& path, qint64 limit) {
    QFile file(path);
    require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= limit,
            "Missing, empty or oversized archive file");
    auto bytes = file.read(limit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= limit && file.atEnd(), "Cannot read bounded archive file");
    return bytes;
}
std::string hash(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}
Json parse(const QByteArray& bytes) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes.toStdString(), [&](int depth, Json::parse_event_t event, Json& item) {
        require(depth <= 128, "Archive JSON nesting exceeds 128");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key) require(keys.back().insert(item.get<std::string>()).second, "Duplicate archive JSON key");
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
bool sha(const Json& value) {
    if (!value.is_string()) return false;
    const auto s = value.get<std::string>();
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
double number(const Json& value) {
    require(value.is_number(), "Archive requires numeric dose/mass/error");
    const double x = value.get<double>();
    require(std::isfinite(x) && x >= 0, "Archive contains negative or nonfinite dose/mass/error");
    return x;
}
bool near(double a, double b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a-b) <= 1e-10 * std::max(std::abs(a), std::abs(b)); }
Json sourceSettings(const Project& p) {
    Json spectra = Json::object();
    for (const auto& s : p.spectra) spectra[s.particle] = {{"energy", s.energy}, {"values", s.values}, {"interpolation", s.interpolation}};
    return {{"quantity", engine::quantityName(p.quantity)}, {"duration_s", p.duration}, {"source_radius_mm", p.radius},
            {"spectra", spectra},{"importance_sampling",p.sourceSampling}};
}
std::map<std::string, Json> targets(const ArchivedRun& run) {
    std::map<std::string, Json> result;
    for (const auto& t : run.result().at("targets")) result.emplace(t.at("id").get<std::string>(), t);
    return result;
}
std::map<std::string, Json> definitions(const Project& p) {
    std::map<std::string, Json> result;
    const auto geometry = engine::compileGeometry(p);
    for (const auto& placement : geometry.placements) {
        const auto& v = geometry.volumes.at(placement.index);
        if (v.sensitive)
        result.emplace(v.id, Json{{"material", v.material}, {"shape", v.shape}, {"size", v.size}, {"wall", v.wall},
            {"world_center", placement.transform.translation}, {"world_rotation", placement.transform.rotation}});
    }
    return result;
}
QString csvString(const std::string& value) {
    auto text = QString::fromStdString(value);
    // Quoting alone does not stop spreadsheet formula interpretation.
    if (!text.isEmpty() && (text.front() == '=' || text.front() == '+' || text.front() == '-' || text.front() == '@' || text.front().isSpace())) text.prepend('\'');
    text.replace('"', "\"\"");
    return '"' + text + '"';
}
}

ArchivedRun loadArchivedRun(const QString& directory) {
    require(!directory.trimmed().isEmpty() && QFileInfo(directory).isDir(), "Choose an existing completed job directory");
    ArchivedRun run;
    run.directory_ = QFileInfo(directory).absoluteFilePath();
    const QDir dir(run.directory_);
    const auto input = read(dir.filePath("project.json"), 8 * 1024 * 1024);
    const auto snapshot = parse(input);
    const auto bytes = read(dir.filePath("result.json"), 64 * 1024 * 1024);
    auto r = parse(bytes);
    auto p = Project::fromJson(snapshot);
    for (const auto& spectrum : p.spectra) {
        const auto& original = spectrum.provenance.at("source_file");
        if (!original.is_null() && original.contains("bytes_base64")) {
            const auto decoded = QByteArray::fromBase64Encoding(QByteArray::fromStdString(original.at("bytes_base64").get<std::string>()), QByteArray::AbortOnBase64DecodingErrors);
            require(decoded && decoded.decoded.size() == original.at("size_bytes").get<qint64>() && hash(decoded.decoded) == original.at("sha256"), "Archive embedded source bytes mismatch");
        }
    }
    const auto identity = engine::sourceFileIdentity(dir.filePath("project.json"), input);
    p.provenance["last_loaded_input"] = identity;
    if (snapshot.at("schema") == "scdat.radiation/1") p.provenance["migration_origin"] = identity;
    p.validate();
    require(r.at("schema") == "scdat.radiation-result/2" && r.at("status") == "completed", "Archive is not a completed result/2");
    require(r.at("project_sha256") == hash(input), "Archive project hash mismatch");
    // last_loaded_input records the historical filename, so relocate directories freely,
    // but retain the canonical project.json snapshot filename.
    require(r.at("project") == p.toJson() && r.at("histories") == p.events, "Archive result/project identity mismatch");
    engine::validateImportanceResult(p,r);
    require(near(number(r.at("expected_primaries")), p.expectedPrimaries()), "Archive source normalization mismatch");
    const auto& audit = r.at("runtime_audit");
    require(audit.at("status") == "ready" && sha(audit.at("manifest_sha256")) && sha(audit.at("build_context_sha256")) && sha(r.at("worker").at("sha256")), "Archive runtime identities missing");
    require(r.at("engine_version").is_string() && !r.at("engine_version").get<std::string>().empty(), "Archive engine version missing");
    for (const auto* key : {"tid", "ddd", "see", "environment_model_execution", "external_physics_validation"})
        require(r.at("capability_status").at(key).is_string(), "Archive capability status missing");
    auto expected = definitions(p);
    require(r.at("targets").is_array() && r.at("targets").size() == expected.size(), "Archive target count mismatch");
    for (const auto& t : r.at("targets")) {
        require(expected.erase(t.at("id").get<std::string>()) == 1, "Duplicate or unexpected archive target");
        require(number(t.at("mass_kg")) > 0, "Archive target mass must be positive");
        const double dose = number(t.at("tid_Gy")), error = number(t.at("tid_standard_error_Gy"));
        require(near(number(t.at("tid_rad")), dose * 100), "Archive Gy/rad mismatch");
        const auto& hits = t.at("nonzero_histories");
        require(hits.is_number_integer() && hits.get<double>() >= 0 && hits.get<double>() <= p.events, "Archive nonzero history count invalid");
        if (dose > 0) {
            require(hits.get<double>() > 0 && near(number(t.at("relative_standard_error")), error / dose), "Archive RSE or nonzero count mismatch");
        } else require(error == 0 && hits == 0 && t.at("relative_standard_error").is_null(), "Archive zero-dose diagnostics inconsistent");
    }
    require(read(dir.filePath("project.json"), 8 * 1024 * 1024) == input && read(dir.filePath("result.json"), 64 * 1024 * 1024) == bytes, "Archive files changed while loading");
    run.project_ = p;
    run.result_ = std::move(r);
    run.projectHash_ = hash(input);
    run.resultHash_ = hash(bytes);
    return run;
}
Json ArchivedRun::summary() const {
    require(!result_.is_null(), "No loaded archive");
    return {{"directory", directory_.toUtf8().toStdString()}, {"name", project_.name}, {"histories", project_.events},
        {"synthetic", project_.synthetic}, {"project_sha256", projectHash_}, {"result_sha256", resultHash_},
        {"engine_version", result_.at("engine_version")}, {"expected_primaries", result_.at("expected_primaries")},
        {"targets", result_.at("targets")}, {"capabilities", result_.at("capability_status")},
        {"verification", "offline_pair_consistency_only_not_producer_authentication"}};
}
Json compareRuns(const ArchivedRun& a, const ArchivedRun& b) {
    Json out = {{"schema", "spacerad.comparison/1"}, {"baseline", a.summary()}, {"candidate", b.summary()},
        {"warnings", Json::array()}, {"rows", Json::array()},
        {"difference_uncertainty", "not_estimated_covariance_unknown"}};
    const bool source = sourceSettings(a.project()) == sourceSettings(b.project());
    const bool physics = a.project().physics == b.project().physics && a.project().cut == b.project().cut &&
        a.result().at("capability_status") == b.result().at("capability_status") &&
        a.result().at("engine_version") == b.result().at("engine_version") &&
        a.result().at("worker").at("sha256") == b.result().at("worker").at("sha256") &&
        a.result().at("runtime_audit").at("manifest_sha256") == b.result().at("runtime_audit").at("manifest_sha256") &&
        a.result().at("runtime_audit").at("build_context_sha256") == b.result().at("runtime_audit").at("build_context_sha256") &&
        a.project().materialSnapshot == b.project().materialSnapshot;
    out["same_source_settings"] = source;
    out["same_physics_assets"] = physics;
    auto& warnings = out["warnings"];
    if (!source) warnings.push_back("source_settings_differ");
    if (!physics) warnings.push_back("physics_or_runtime_assets_differ");
    if (a.project().environmentMetadata != b.project().environmentMetadata || a.project().synthetic != b.project().synthetic || a.project().environmentSource != b.project().environmentSource)
        warnings.push_back("environment_provenance_differs");
    warnings.push_back("difference_uncertainty_not_estimated_covariance_unknown");
    auto at = targets(a), bt = targets(b);
    auto ad = definitions(a.project()), bd = definitions(b.project());
    std::set<std::string> ids;
    for (const auto& t : at) ids.insert(t.first);
    for (const auto& t : bt) ids.insert(t.first);
    for (const auto& id : ids) {
        const bool both = at.count(id) && bt.count(id);
        const bool same = both && ad.at(id) == bd.at(id) && near(at.at(id).at("mass_kg"), bt.at(id).at("mass_kg"));
        Json row = {{"id", id}, {"status", !at.count(id) ? "candidate_only" : !bt.count(id) ? "baseline_only" : !same ? "target_changed" : "matched"},
            {"baseline", at.count(id) ? at.at(id) : Json(nullptr)}, {"candidate", bt.count(id) ? bt.at(id) : Json(nullptr)},
            {"delta_Gy", nullptr}, {"ratio_B_over_A", nullptr}, {"controlled_comparison", both && same && source && physics}};
        if (both) {
            const double av = at.at(id).at("tid_Gy"), bv = bt.at(id).at("tid_Gy");
            const double delta = bv-av, ratio = av > 0 ? bv/av : 0;
            require(std::isfinite(delta) && std::isfinite(ratio), "Comparison arithmetic overflow");
            row["delta_Gy"] = delta;
            if (av > 0) row["ratio_B_over_A"] = ratio;
            else warnings.push_back("zero_baseline_ratio_undefined:" + id);
        }
        out["rows"].push_back(std::move(row));
    }
    return out;
}
QByteArray comparisonCsv(const Json& comparison) {
    require(comparison.at("schema") == "spacerad.comparison/1", "Unsupported comparison schema");
    QString csv = "id,status,controlled_comparison,A_mass_kg,A_tid_Gy,A_SE_Gy,B_mass_kg,B_tid_Gy,B_SE_Gy,delta_Gy,ratio_B_over_A\n";
    for (const auto& row : comparison.at("rows")) {
        csv += csvString(row.at("id")) + ',' + csvString(row.at("status")) + ',' + QString::fromStdString(row.at("controlled_comparison").dump());
        for (auto side : {"baseline", "candidate"}) for (auto field : {"mass_kg", "tid_Gy", "tid_standard_error_Gy"})
            csv += ',' + (row.at(side).is_null() ? QString{} : QString::fromStdString(row.at(side).at(field).dump()));
        for (auto field : {"delta_Gy", "ratio_B_over_A"}) csv += ',' + (row.at(field).is_null() ? QString{} : QString::fromStdString(row.at(field).dump()));
        csv += '\n';
    }
    return csv.toUtf8();
}
}
