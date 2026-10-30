#include "RadiationProject.h"
#include "Geometry.h"
#include "Voxels.h"
#include "runtime/VoxelStatistics.h"
#include "runtime/VoxelExport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace spacerad::engine;

namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F&& f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
bool near(double a, double b, double tolerance = 1e-11) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <= tolerance * std::max({1.0, std::abs(a), std::abs(b)});
}
Spectrum spectrum() {
    Spectrum s;
    s.particle = "proton";
    s.energy = {1.0, 2.0};
    s.values = {1.0, 1.0};
    s.prepare();
    return s;
}
Json voxelSettings(Json counts = Json::array({2, 1, 1})) {
    return {{"schema", "scdat.voxel-scoring/1"}, {"volume_id", "SiTarget"}, {"counts", std::move(counts)},
            {"edep_edges_MeV", Json::array({0.0, 1.0, 2.0})},
            {"quality", {{"min_nonzero_histories", 2}, {"max_relative_error", 1.0}}}};
}
Project projectFixture(int events = 4) {
    Project p = Project::demo();
    p.name = "Synthetic voxel contract fixture";
    p.synthetic = true;
    p.events = events;
    p.radius = 500.0;
    p.volumes = {{"SiTarget", "G4_Si", {10, 0, 0}, {20, 10, 10}, 0.0, true, "Bus", {0, 0, 0}}};
    p.assemblies = {{"Bus", "", {100, 20, 0}, {0, 0, 90}, true}};
    p.templates.clear();
    p.instances.clear();
    p.spectra = {spectrum()};
    p.voxelScoring = voxelSettings();
    p.validate();
    return p;
}
Moments moments(const std::vector<double>& events) {
    Moments result;
    for (double value : events) result.add(value);
    return result;
}
Json momentJson(const std::vector<double>& events) {
    const Moments m = moments(events);
    return {{"count", m.count}, {"nonzero_histories", m.nonzero}, {"mean_MeV", m.mean}, {"m2_MeV2", m.m2}};
}
Json cellMoments(const std::vector<double>& ion) {
    std::vector<double> total, nonionizing;
    total.reserve(ion.size());
    nonionizing.reserve(ion.size());
    for (double value : ion) {
        total.push_back(value * 1.5);
        nonionizing.push_back(value * 0.5);
    }
    return {{"ionizing", momentJson(ion)}, {"total", momentJson(total)}, {"nonionizing", momentJson(nonionizing)}};
}
Json momentsFor(const std::vector<double>& a, const std::vector<double>& b) {
    return Json::array({cellMoments(a), cellMoments(b)});
}
Json auditFor(const Project& p) {
    Json audit = voxelLayout(p);
    audit["cell_mass_kg"] = 0.00233;
    audit["target_mass_kg"] = 0.00466;
    audit["density_kg_m3"] = 2330.0;
    return audit;
}
Json perfectlyCorrelatedWholeMoments(const Json& cells, const char* channel, int histories) {
    // Every fixture uses paired event energies b=a+c for a constant c.
    // Thus whole-event sums are 2a+c and M2(sum)=4*M2(a), not M2(a)+M2(b).
    const auto& first = cells.at(0).at(channel);
    const auto& second = cells.at(1).at(channel);
    const double firstMean = first.at("mean_MeV").get<double>();
    const double secondMean = second.at("mean_MeV").get<double>();
    const double firstM2 = first.at("m2_MeV2").get<double>();
    const double secondM2 = second.at("m2_MeV2").get<double>();
    if (!near(firstM2, secondM2))
        throw std::runtime_error("Perfectly correlated fixture cells must have equal M2");
    return {{"count", histories}, {"nonzero_histories", histories},
            {"mean_MeV", firstMean + secondMean}, {"m2_MeV2", 4.0 * firstM2}};
}
Json resultFor(const Project& p, const Json& cellScores, int histories, const Json& audit) {
    const Json field = makeVoxelField(p, audit, cellScores, histories);
    const double targetMass = audit.at("target_mass_kg").get<double>();
    const Json wholeIonizing = perfectlyCorrelatedWholeMoments(cellScores, "ionizing", histories);
    const Json wholeTotal = perfectlyCorrelatedWholeMoments(cellScores, "total", histories);
    const Json wholeNonionizing = perfectlyCorrelatedWholeMoments(cellScores, "nonionizing", histories);
    const double factor = 1.602176634e-13 * p.expectedPrimaries() / targetMass;
    const double targetDose = wholeIonizing.at("mean_MeV").get<double>() * factor;
    const double targetSe = std::sqrt(wholeIonizing.at("m2_MeV2").get<double>() /
                                      (histories * double(histories - 1))) * factor;
    Json result = Json::object();
    result["schema"] = "scdat.radiation-result/2";
    result["status"] = "completed";
    result["histories"] = histories;
    result["project"] = p.toJson();
    result["voxel_field"] = field;
    Json target = Json::object();
    target["id"] = "SiTarget";
    target["mass_kg"] = targetMass;
    target["tid_Gy"] = targetDose;
    target["tid_standard_error_Gy"] = targetSe;
    target["relative_standard_error"] = targetDose > 0.0 ? Json(targetSe / targetDose) : Json(nullptr);
    result["targets"] = Json::array({target});
    result["score_moments"] = Json::object();
    result["score_moments"]["SiTarget"] = {{"ionizing", wholeIonizing}, {"total", wholeTotal},
                                             {"nonionizing", wholeNonionizing}};
    result["edep_spectra"] = Json::object();
    result["edep_spectra"]["SiTarget"] = makeEdepSpectrum({0.0, 1.0, 2.0}, {histories - 1, 0}, 1, histories);
    return result;
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read exported file");
    return file.readAll();
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const Project project = projectFixture();
        const Json settings = project.voxelScoring;
        const auto spec = voxelSpec(settings);
        check(settings.size() == 5 && spec.volumeId == "SiTarget" && spec.counts == std::array<int, 3>{2, 1, 1} &&
              spec.edges == std::vector<double>{0.0, 1.0, 2.0},
              "Strict five-field voxel settings must parse the exact schema, grid and energy edges");
        check(voxelSpec(voxelSettings(Json::array({16, 16, 16}))).size() == 4096,
              "Voxel grids up to the exact 4096-cell limit must be accepted");
        check(Project::fromJson(project.toJson()).toJson() == project.toJson() &&
              project.toJson().at("schema") == "scdat.radiation/7" &&
              Project::fromJson(project.toJson()).voxelScoring == settings,
              "Voxel-scoring project schema v7 must round-trip without changing its settings");

        for (const auto& invalid : std::vector<Json>{
                 [] { auto j = voxelSettings(); j["extra"] = 1; return j; }(),
                 [] { auto j = voxelSettings(); j.erase("quality"); return j; }(),
                 [] { auto j = voxelSettings(); j["counts"] = {2.0, 1, 1}; return j; }(),
                 [] { auto j = voxelSettings(); j["counts"] = {true, 1, 1}; return j; }(),
                 [] { auto j = voxelSettings(); j["counts"] = {33, 1, 1}; return j; }(),
                 [] { auto j = voxelSettings(Json::array({17, 16, 16})); return j; }(),
                 [] { auto j = voxelSettings(); j["edep_edges_MeV"] = {0, 1, 1}; return j; }(),
                 [] { auto j = voxelSettings(); j["edep_edges_MeV"] = {0, 2, 1}; return j; }(),
                 [] { auto j = voxelSettings(); j["edep_edges_MeV"] = {0.1, 1, 2}; return j; }(),
                 [] { auto j = voxelSettings(); j["edep_edges_MeV"] = {0, true}; return j; }(),
                 [] { auto j = voxelSettings(); j["edep_edges_MeV"] = {0, 1e10}; return j; }(),
                 [] { auto j = voxelSettings(); j["quality"]["max_relative_error"] = 0.0; return j; }(),
                 [] { auto j = voxelSettings(); j["quality"]["max_relative_error"] = 1.1; return j; }(),
                 [] { auto j = voxelSettings(); j["quality"]["unknown"] = 1; return j; }(),
                 [] { auto j = voxelSettings(); j["quality"]["min_nonzero_histories"] = 1; return j; }()}) {
            rejects([&] { (void)voxelSpec(invalid); }, "Malformed voxel settings must be rejected");
        }
        auto oldVersion = project.toJson();
        oldVersion["schema"] = "scdat.radiation/6";
        rejects([&] { (void)Project::fromJson(oldVersion); }, "Older project schemas must reject a voxel_scoring field");
        auto nonVoxel = project;
        nonVoxel.voxelScoring = nullptr;
        check(nonVoxel.toJson().at("schema") != "scdat.radiation/7",
              "Projects without voxel scoring must retain a pre-v7 schema");

        const Json layout = voxelLayout(project);
        check(layout.at("spacing_mm") == Json::array({10.0, 10.0, 10.0}) &&
              layout.at("origin_local_mm") == Json::array({-10.0, -5.0, -5.0}) &&
              near(layout.at("cell_volume_mm3"), 1000.0),
              "Voxel layout must use exact local box bounds, spacing and cell volume");
        check(near(layout.at("world_center_mm")[0], 100.0) && near(layout.at("world_center_mm")[1], 30.0) &&
              near(layout.at("world_center_mm")[2], 0.0) && near(layout.at("world_rotation")[0][1], -1.0) &&
              near(layout.at("world_rotation")[1][0], 1.0),
              "Voxel layout must follow the actual parent assembly transform");
        check(near(0.00233 * 2.0, 0.00466) && near(2330.0 * 1000.0e-9, 0.00233),
              "Fixture cell masses must match the actual homogeneous Si box volume and density");

        for (const auto& invalidGeometry : std::vector<Project>{
                 [] { auto p = projectFixture(); p.volumes[0].wall = 1.0; return p; }(),
                 [] { auto p = projectFixture(); p.volumes[0].sensitive = false; return p; }(),
                 [] { auto p = projectFixture(); p.volumes[0].shape = "cylinder"; return p; }()}) {
            rejects([&] { validateVoxels(invalidGeometry); },
                    "Voxel scoring must require a homogeneous solid sensitive box");
        }
        auto tooSmall = project;
        tooSmall.volumes[0].size[0] = 0.003;
        tooSmall.voxelScoring = voxelSettings(Json::array({32, 1, 1}));
        rejects([&] { validateVoxels(tooSmall); }, "Voxel cell widths below 1e-4 mm must be rejected");

        Moments padded;
        padded.add(0.0);
        padded.add(1.0);
        padZeroHistories(padded, 4);
        padded.add(2.0);
        padded.add(0.0);
        Moments explicitZeros;
        for (double value : {0.0, 1.0, 0.0, 0.0, 2.0, 0.0}) explicitZeros.add(value);
        check(padded.count == explicitZeros.count && padded.nonzero == explicitZeros.nonzero &&
              near(padded.mean, explicitZeros.mean) && near(padded.m2, explicitZeros.m2),
              "Zero-history padding must match explicit leading, interior and trailing zero events");
        rejects([&] { padZeroHistories(padded, padded.count - 1); }, "Zero-history padding cannot shrink a moment count");
        rejects([&] { padZeroHistories(padded, 100000001); }, "Zero-history padding must enforce the event-count ceiling");

        Json audit = auditFor(project);
        const Json singleCellScores = momentsFor({0, 1, 2, 1}, {1, 2, 3, 2});
        auto invalidScores = singleCellScores;
        invalidScores[0]["ionizing"]["count"] = 3;
        rejects([&] { (void)makeVoxelField(project, audit, invalidScores, 4); },
                "Cell moment count must equal the field's completed histories");
        invalidScores = singleCellScores;
        invalidScores[0]["ionizing"]["m2_MeV2"] = -1.0;
        rejects([&] { (void)makeVoxelField(project, audit, invalidScores, 4); },
                "Negative raw event moments must be rejected");
        Json invalidAudit = audit;
        invalidAudit["cell_mass_kg"] = 0.003;
        rejects([&] { (void)makeVoxelField(project, invalidAudit, singleCellScores, 4); },
                "Cell mass inconsistent with the real target density and volume must be rejected");
        const Json result = resultFor(project, singleCellScores, 4, audit);
        const Json& field = result.at("voxel_field");
        check(field.at("schema") == "scdat.radiation-voxel-field/1" && field.at("rows").size() == 2 &&
              field.at("rows")[0].at("ijk") == Json::array({0, 0, 0}) &&
              field.at("rows")[1].at("ijk") == Json::array({1, 0, 0}),
              "Voxel field must emit complete rows in x-fastest flattening order");
        check(near(field.at("target_mass_kg"), 0.00466) && near(field.at("cell_mass_kg"), 0.00233) &&
              near(field.at("rows")[0].at("mass_kg"), 0.00233) &&
              near(field.at("rows")[0].at("tid_standard_error_Gy"),
                   std::sqrt(2.0 / 12.0) * 1.602176634e-13 * project.expectedPrimaries() / 0.00233),
              "Voxel field must use actual homogeneous cell mass and convert moment SE to Gy");
        const Moments wholeEvents = moments({1, 3, 5, 3});
        const double expectedWholeSe = wholeEvents.standardError() * 1.602176634e-13 *
                                       project.expectedPrimaries() / 0.00466;
        const double independentCellQuadrature = std::sqrt(
            std::pow(field.at("rows")[0].at("tid_standard_error_Gy").get<double>() * 0.5, 2) +
            std::pow(field.at("rows")[1].at("tid_standard_error_Gy").get<double>() * 0.5, 2));
        check(near(result.at("targets")[0].at("tid_standard_error_Gy"), expectedWholeSe) &&
              result.at("targets")[0].at("tid_standard_error_Gy").get<double>() > independentCellQuadrature,
              "Whole-target SE must be computed from event-wise cell sums, not independent mass-weighted cell errors");
        check(field.at("caution").get<std::string>().find("cell covariance is not exported") != std::string::npos &&
              field.at("caution").get<std::string>().find("sum of cell standard errors is not target error") != std::string::npos,
              "Voxel field must state that cell errors cannot be summed as a target-level error without covariance");
        validateVoxelResult(project, result);

        auto zeroScores = momentsFor({0, 0, 0, 0}, {1, 2, 3, 2});
        const Json zeroField = makeVoxelField(project, audit, zeroScores, 4);
        check(zeroField.at("rows")[0].at("quality") == "zero_hit" &&
              zeroField.at("rows")[0].at("relative_standard_error").is_null() &&
              zeroField.at("rows")[0].at("tid_Gy") == 0.0 &&
              zeroField.at("rows")[1].at("quality") != "zero_hit",
              "Zero-hit cells must retain zero dose and null RSE without implying zero risk");

        auto malformed = result;
        malformed["histories"] = 5;
        rejects([&] { validateVoxelResult(project, malformed); }, "Voxel result history mismatch must be rejected");
        malformed = result; malformed["voxel_field"]["rows"][0]["mass_kg"] = 0.003;
        rejects([&] { validateVoxelResult(project, malformed); }, "Voxel row mass mismatch must be rejected");
        malformed = result; malformed["voxel_field"]["rows"][0]["score_moments"]["ionizing"]["count"] = 3;
        rejects([&] { validateVoxelResult(project, malformed); }, "Voxel moment count mismatch must be rejected");
        malformed = result; malformed["voxel_field"]["rows"][0]["quality"] = "unknown";
        rejects([&] { validateVoxelResult(project, malformed); }, "Unknown voxel quality categories must be rejected");
        malformed = result; malformed["voxel_field"]["rows"][0]["tid_Gy"] = 0.0;
        rejects([&] { validateVoxelResult(project, malformed); }, "Voxel dose inconsistent with raw moments must be rejected");
        malformed = result; malformed["score_moments"]["SiTarget"]["ionizing"]["mean_MeV"] = malformed.at("score_moments").at("SiTarget").at("ionizing").at("mean_MeV").get<double>() + 0.1;
        rejects([&] { validateVoxelResult(project, malformed); }, "Cell-to-whole-target energy imbalance must be rejected");
        malformed = result; malformed["targets"][0]["mass_kg"] = 0.005;
        rejects([&] { validateVoxelResult(project, malformed); }, "Whole-target actual mass mismatch must be rejected");
        malformed = result; malformed["edep_spectra"]["SiTarget"]["counts"][0] = 4;
        rejects([&] { validateVoxelResult(project, malformed); }, "Energy histogram counts plus overflow must equal histories");

        const Json spectrumJson = makeEdepSpectrum({0.0, 1.0, 2.0}, {3, 0}, 1, 4);
        check(spectrumJson.at("counts") == Json::array({3, 0}) && spectrumJson.at("overflow") == 1 &&
              near(spectrumJson.at("probabilities")[0], 0.75) && near(spectrumJson.at("overflow_probability"), 0.25),
              "Energy-deposit histogram must expose count-conserving event probabilities and overflow");
        rejects([&] { (void)makeEdepSpectrum({0.0, 1.0}, {1}, 0, 4); },
                "Energy histogram with missing event counts must be rejected");

        Project poolProject = projectFixture(5);
        Project childA = poolProject; childA.events = 2;
        Project childB = poolProject; childB.events = 3;
        const Json auditForChildren = auditFor(poolProject);
        const Json scoresA = momentsFor({1, 2}, {2, 3});
        const Json scoresB = momentsFor({3, 4, 5}, {4, 5, 6});
        const Json batchA = resultFor(childA, scoresA, 2, auditForChildren);
        const Json batchB = resultFor(childB, scoresB, 3, auditForChildren);
        const Json combinedScores = momentsFor({1, 2, 3, 4, 5}, {2, 3, 4, 5, 6});
        Json merged = resultFor(poolProject, combinedScores, 5, auditForChildren);
        mergeVoxelResults(poolProject, {batchA, batchB}, merged);
        check(merged.at("voxel_field").at("histories") == 5 &&
              near(merged.at("voxel_field").at("rows")[0].at("score_moments").at("ionizing").at("mean_MeV"), 3.0) &&
              near(merged.at("score_moments").at("SiTarget").at("ionizing").at("mean_MeV"), 7.0),
              "Unequal cell-event pools must merge by Chan and conserve whole-target mean");
        const double pooledCellSe = merged.at("voxel_field").at("rows")[0].at("tid_standard_error_Gy");
        const double sumOfBatchErrors = batchA.at("voxel_field").at("rows")[0].at("tid_standard_error_Gy").get<double>() +
                                        batchB.at("voxel_field").at("rows")[0].at("tid_standard_error_Gy").get<double>();
        check(pooledCellSe > 0.0 && std::isfinite(pooledCellSe) && !near(pooledCellSe, sumOfBatchErrors),
              "Pooled cell SE must come from pooled event moments, not a sum of batch or cell errors");

        Project exportProject = projectFixture();
        exportProject.assemblies.clear();
        exportProject.volumes[0].parent.clear();
        exportProject.volumes[0].center = {0, 0, 0};
        exportProject.validate();
        const Json exportAudit = auditFor(exportProject);
        const Json exportResult = resultFor(exportProject, singleCellScores, 4, exportAudit);
        const QString vti = voxelVti(exportResult.at("voxel_field"));
        const QString csv = voxelCsv(exportResult.at("voxel_field"));
        // Zero-angle Euler expressions can preserve IEEE -0 in the matrix;
        // validate the serialized matrix numerically instead of its spelling.
        const int directionStart = vti.indexOf(QStringLiteral("Direction=\""));
        QStringList directionTokens;
        if (directionStart >= 0) {
            const int valueStart = directionStart + QStringLiteral("Direction=\"").size();
            const int valueEnd = vti.indexOf(QLatin1Char('"'), valueStart);
            if (valueEnd > valueStart)
                directionTokens = vti.mid(valueStart, valueEnd - valueStart).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        bool directionMatches = directionTokens.size() == 9;
        for (int i = 0; directionMatches && i < 9; ++i) {
            bool parsed = false;
            const double actual = directionTokens[i].toDouble(&parsed);
            const double expected = exportAudit.at("world_rotation")[i / 3][i % 3].get<double>();
            directionMatches = parsed && near(actual, expected);
        }
        check(vti.contains(QStringLiteral("<VTKFile type=\"ImageData\"")) &&
              vti.contains(QStringLiteral("WholeExtent=\"0 2 0 1 0 1\"")) &&
              directionMatches,
              "VTI must serialize the oriented image extent and direction matrix");
        check(csv.startsWith(QStringLiteral("index,i,j,k,localcenter_x_mm")) &&
              csv.indexOf(QStringLiteral("0,0,0,0,")) < csv.indexOf(QStringLiteral("1,1,0,0,")),
              "CSV must retain deterministic x-fastest row ordering and coordinate columns");

        QTemporaryDir temporary;
        check(temporary.isValid(), "Temporary export directory must be available");
        const QString output = temporary.path() + QStringLiteral("/voxel-export");
        exportVoxelFieldFiles(exportResult, output);
        const Json manifest = Json::parse(readFile(output + QStringLiteral("/manifest.json")).toStdString());
        check(manifest.at("schema") == "scdat.radiation-voxel-export/1" && manifest.at("files").size() == 3,
              "Voxel export must publish a manifest describing the three payload files");
        for (const auto& file : manifest.at("files")) {
            const QString path = output + QLatin1Char('/') + QString::fromStdString(file.at("path").get<std::string>());
            const QByteArray bytes = readFile(path);
            check(std::uint64_t(bytes.size()) == file.at("size_bytes").get<std::uint64_t>() &&
                  QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString() ==
                      file.at("sha256").get<std::string>(),
                  "Voxel export manifest size and SHA-256 must match each payload");
        }
        rejects([&] { exportVoxelFieldFiles(exportResult, output); },
                "Voxel export must refuse to overwrite an existing directory");

        std::cout << "Voxel contracts: " << checks
                  << " checks passed; schema/layout, moments, fields, pooling, spectra and exports\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
