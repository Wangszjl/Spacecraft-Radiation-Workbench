#include "RadiationProject.h"
#include "runtime/BatchStatistics.h"
#include "runtime/ResponseMatrix.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace spacerad::engine;

namespace {
int checks = 0;

void check(bool passed, const char* message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}

template<class F>
void rejects(F&& operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

bool near(double actual, long double expected, long double relative = 2e-12L,
          long double absolute = 1e-20L) {
    const long double value = actual;
    return std::isfinite(actual) && std::abs(value - expected) <= absolute + relative * std::abs(expected);
}

Spectrum binnedSpectrum(const std::string& particle, const std::vector<double>& values,
                        const std::vector<double>& edges = {1.0, 2.0, 4.0}) {
    Spectrum spectrum;
    spectrum.particle = particle;
    spectrum.interpolation = "bin_constant";
    spectrum.energy = edges;
    spectrum.values = values;
    spectrum.prepare();
    return spectrum;
}

Project projectFixture(int events = 4) {
    Project project = Project::demo();
    project.name = "Synthetic response matrix contract fixture";
    project.environmentSource = "synthetic response basis fixture";
    project.synthetic = true;
    project.duration = 7.0;
    project.radius = 100.0;
    project.cut = 0.025;
    project.events = events;
    project.seed = 19780503;
    project.physics = "FTFP_BERT_EMZ";
    project.assemblies.clear();
    project.templates.clear();
    project.instances.clear();
    project.volumes = {{"Target_Si", "G4_Si", {0, 0, 0}, {10, 10, 10}, 0, true}};
    project.spectra = {binnedSpectrum("proton", {1.0, 4.0}),
                       binnedSpectrum("e-", {2.0, 3.0})};
    project.voxelScoring = nullptr;
    project.validate();
    return project;
}

Json optionsJson(std::vector<std::string> particles = {"proton", "e-"},
                 std::vector<double> edges = {1.0, 2.0, 4.0},
                 int historiesPerCase = 4, int historiesPerBatch = 4, int maxParallel = 2) {
    return {{"schema", "scdat.radiation-response-options/1"},
            {"particles", std::move(particles)}, {"energy_edges_MeV", std::move(edges)},
            {"histories_per_case", historiesPerCase}, {"histories_per_batch", historiesPerBatch},
            {"max_parallel", maxParallel}};
}

struct ReferenceMoments {
    long double mean = 0;
    long double m2 = 0;
    int nonzero = 0;
};

ReferenceMoments referenceMoments(const std::vector<double>& events) {
    ReferenceMoments result;
    long double sum = 0;
    for (double energy : events) {
        sum += static_cast<long double>(energy);
        if (energy != 0) ++result.nonzero;
    }
    result.mean = sum / static_cast<long double>(events.size());
    for (double energy : events) {
        const long double delta = static_cast<long double>(energy) - result.mean;
        result.m2 += delta * delta;
    }
    return result;
}

Json eventMoments(const std::vector<double>& events) {
    const auto moments = referenceMoments(events);
    return {{"count", events.size()}, {"nonzero_histories", moments.nonzero},
            {"mean_MeV", static_cast<double>(moments.mean)},
            {"m2_MeV2", static_cast<double>(moments.m2)}};
}

std::vector<double> ionEvents(int index, bool zero = false) {
    if (zero) return {0, 0, 0, 0};
    switch (index) {
    case 0: return {0, 1, 2, 1};       // mean 1, M2 2
    case 1: return {1, 1, 3, 3};       // mean 2, M2 4
    case 2: return {1, 1, 3, 3};       // mean 2, M2 4
    default: return {0, 2, 4, 2};      // mean 2, M2 8
    }
}

Json makeCaseResult(const ResponseCase& responseCase, bool zeroIonizing = false) {
    const auto ionizing = ionEvents(responseCase.index, zeroIonizing);
    std::vector<double> total, nonionizing;
    total.reserve(ionizing.size());
    nonionizing.reserve(ionizing.size());
    for (double energy : ionizing) {
        total.push_back(energy * 1.5);
        nonionizing.push_back(energy * 0.5);
    }
    const int histories = static_cast<int>(ionizing.size());
    return {{"status", "completed"}, {"project", responseCase.project.toJson()},
            {"histories", histories}, {"expected_primaries", responseCase.project.expectedPrimaries()},
            {"targets", Json::array({{{"id", "Target_Si"}, {"mass_kg", 0.00233}}})},
            {"score_moments", {{"Target_Si", {
                {"ionizing", eventMoments(ionizing)},
                {"total", eventMoments(total)},
                {"nonionizing", eventMoments(nonionizing)}}}}}};
}

std::vector<Json> allCaseResults(const std::vector<ResponseCase>& plan, int zeroCase = -1) {
    std::vector<Json> results;
    results.reserve(plan.size());
    for (const auto& responseCase : plan)
        results.push_back(makeCaseResult(responseCase, responseCase.index == zeroCase));
    return results;
}

const Spectrum& spectrumFor(const Project& project, const std::string& particle) {
    const auto found = std::find_if(project.spectra.begin(), project.spectra.end(),
        [&](const Spectrum& spectrum) { return spectrum.particle == particle; });
    if (found == project.spectra.end()) throw std::runtime_error("Fixture source spectrum missing");
    return *found;
}

struct WeightedReference {
    long double doseGy = 0;
    long double standardErrorGy = 0;
};

WeightedReference weightedReference(const Project& project, const std::vector<ResponseCase>& plan,
                                    double massKg, int zeroCase = -1) {
    constexpr long double mevToJoule = 1.602176634e-13L;
    const long double areaCm2 = pi * std::pow(project.radius / 10.0L, 2);
    long double dose = 0;
    long double variance = 0;
    for (const auto& responseCase : plan) {
        const auto& spectrum = spectrumFor(project, responseCase.particle);
        const auto upper = std::upper_bound(spectrum.energy.begin(), spectrum.energy.end(), responseCase.low);
        const std::size_t bin = static_cast<std::size_t>(upper - spectrum.energy.begin() - 1);
        const long double width = responseCase.high - responseCase.low;
        const long double exposureFactor = project.quantity == SpectrumQuantity::Flux ? project.duration : 1.0;
        const long double weight = areaCm2 * width * spectrum.values.at(bin) * exposureFactor;
        const auto moments = referenceMoments(ionEvents(responseCase.index, responseCase.index == zeroCase));
        const long double histories = 4;
        const long double standardError = std::sqrt(moments.m2 / (histories * (histories - 1)));
        dose += weight * moments.mean * mevToJoule / massKg;
        variance += std::pow(weight * standardError * mevToJoule / massKg, 2);
    }
    return {dose, std::sqrt(variance)};
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const Project project = projectFixture();
        const auto parsed = ResponseOptions::fromJson(optionsJson());
        const auto canonicalOptions = parsed.toJson();
        check(canonicalOptions.size() == 6 && canonicalOptions.at("schema") == "scdat.radiation-response-options/1" &&
              parsed.particles == std::vector<std::string>({"e-", "proton"}) &&
              parsed.edges == std::vector<double>({1.0, 2.0, 4.0}),
              "Strict response options must normalize particle order and preserve exact energy bins");
        check(ResponseOptions::fromJson(canonicalOptions).toJson() == canonicalOptions,
              "Response options must round-trip in canonical form");
        const auto batch = parsed.batchOptions();
        check(batch.maxHistories == parsed.historiesPerCase &&
              batch.historiesPerBatch == parsed.historiesPerBatch && batch.maxParallel == parsed.maxParallel &&
              batch.maxSeconds == 0,
              "Response cases must use the requested fixed batch budget without a time limit");

        for (const auto& malformed : std::vector<Json>{
                 [] { auto j = optionsJson(); j["extra"] = 1; return j; }(),
                 [] { auto j = optionsJson(); j.erase("particles"); return j; }(),
                 [] { auto j = optionsJson(); j["schema"] = "scdat.radiation-response-options/2"; return j; }(),
                 [] { auto j = optionsJson(); j["histories_per_case"] = true; return j; }(),
                 [] { auto j = optionsJson(); j["histories_per_batch"] = 1; return j; }(),
                 [] { auto j = optionsJson(); j["max_parallel"] = 0; return j; }(),
                 [] { auto j = optionsJson(); j["energy_edges_MeV"] = {1.0, 2.0, 2.0}; return j; }(),
                 [] { auto j = optionsJson(); j["energy_edges_MeV"] = {1.0, 3.0, 2.0}; return j; }(),
                 [] { auto j = optionsJson(); j["particles"] = {"muon"}; return j; }()}) {
            rejects([&] { (void)ResponseOptions::fromJson(malformed); },
                    "Malformed response options must be rejected");
        }
        rejects([&] { (void)ResponseOptions::fromJson(optionsJson({"proton", "e-", "proton"})); },
                "Duplicate response particles must be rejected explicitly");

        const auto plan = planResponseCases(project, parsed);
        check(plan.size() == 4, "Two source species and two bins must produce four independent cases");
        std::set<int> caseSeeds;
        const std::array<std::string, 4> expectedParticles{"e-", "e-", "proton", "proton"};
        const std::array<double, 4> expectedLow{1.0, 2.0, 1.0, 2.0};
        const std::array<double, 4> expectedHigh{2.0, 4.0, 2.0, 4.0};
        for (std::size_t i = 0; i < plan.size(); ++i) {
            const auto& responseCase = plan[i];
            check(responseCase.index == static_cast<int>(i) && responseCase.particle == expectedParticles[i] &&
                  responseCase.low == expectedLow[i] && responseCase.high == expectedHigh[i],
                  "Response cases must be ordered by canonical particle and ascending energy bin");
            check(responseCase.project.quantity == SpectrumQuantity::Fluence &&
                  responseCase.project.spectra.size() == 1 &&
                  responseCase.project.spectra.front().particle == responseCase.particle &&
                  responseCase.project.spectra.front().interpolation == "bin_constant" &&
                  near(responseCase.project.spectra.front().integral, 1.0L) &&
                  responseCase.project.events == parsed.historiesPerCase &&
                  near(responseCase.project.expectedPrimaries(), pi * std::pow(project.radius / 10.0, 2)),
                  "Each basis project must represent unit-integral fluence in exactly one particle/bin");
            check(caseSeeds.insert(static_cast<int>(responseCase.project.seed)).second &&
                  responseCase.project.seed >= 1 && responseCase.project.seed <= 2147483646,
                  "Response cases must use distinct valid seeds");
        }
        const auto batchPlan = planBatches(plan.front().project, batch);
        check(batchPlan.size() == 1 && batchPlan.front().histories == parsed.historiesPerCase,
              "Response batch options must cover each case exactly once at its fixed history budget");

        auto wrongSpectrum = project;
        wrongSpectrum.spectra[0].interpolation = "linear";
        wrongSpectrum.spectra[0].energy = {1.0, 4.0};
        wrongSpectrum.spectra[0].values = {1.0, 2.0};
        wrongSpectrum.spectra[0].prepare();
        wrongSpectrum.validate();
        rejects([&] { (void)planResponseCases(wrongSpectrum, parsed); },
                "Linear source spectra must not be converted silently into response bins");
        auto wrongEdges = project;
        wrongEdges.spectra[0] = binnedSpectrum("proton", {1.0, 4.0}, {1.0, 2.5, 4.0});
        wrongEdges.validate();
        rejects([&] { (void)planResponseCases(wrongEdges, parsed); },
                "Response bins must match source bin-constant spectrum edges exactly");
        auto wrongParticles = project;
        wrongParticles.spectra.pop_back();
        wrongParticles.validate();
        rejects([&] { (void)planResponseCases(wrongParticles, parsed); },
                "Response particle set must equal the project spectrum particle set");
        auto voxelProject = project;
        voxelProject.voxelScoring = {{"schema", "scdat.voxel-scoring/1"}, {"volume_id", "Target_Si"},
            {"counts", {1, 1, 1}}, {"edep_edges_MeV", {0.0, 1.0}},
            {"quality", {{"min_nonzero_histories", 2}, {"max_relative_error", 0.1}}}};
        voxelProject.validate();
        rejects([&] { (void)planResponseCases(voxelProject, parsed); },
                "Response cache planning must reject voxel scoring rather than drop it");

        auto tooManyCasesOptions = optionsJson(
            {"e-", "proton", "alpha", "gamma", "neutron"}, {1, 2, 3, 4, 5, 6, 7, 8}, 2, 2, 2);
        Project fiveSpecies = projectFixture(2);
        fiveSpecies.spectra = {binnedSpectrum("e-", {1,1,1,1,1,1,1}, {1,2,3,4,5,6,7,8}),
                               binnedSpectrum("proton", {1,1,1,1,1,1,1}, {1,2,3,4,5,6,7,8}),
                               binnedSpectrum("alpha", {1,1,1,1,1,1,1}, {1,2,3,4,5,6,7,8}),
                               binnedSpectrum("gamma", {1,1,1,1,1,1,1}, {1,2,3,4,5,6,7,8}),
                               binnedSpectrum("neutron", {1,1,1,1,1,1,1}, {1,2,3,4,5,6,7,8})};
        fiveSpecies.validate();
        rejects([&] { (void)planResponseCases(fiveSpecies, ResponseOptions::fromJson(tooManyCasesOptions)); },
                "Response plans above the 32-case limit must be rejected");
        const std::vector<double> manyEdges = [] {
            std::vector<double> edges;
            for (int i = 1; i <= 33; ++i) edges.push_back(i);
            return edges;
        }();
        auto tooManyHistories = optionsJson({"e-"}, manyEdges, 3125001, 3125001, 1);
        Project manyBins = projectFixture(2);
        manyBins.spectra = {binnedSpectrum("e-", std::vector<double>(32, 1.0), manyEdges),
                            binnedSpectrum("proton", std::vector<double>(32, 1.0), manyEdges)};
        manyBins.validate();
        rejects([&] { (void)planResponseCases(manyBins, ResponseOptions::fromJson(tooManyHistories)); },
                "Response plans above 100 million total histories must be rejected");
        auto tooManyBatches = optionsJson({"e-"}, {1.0, 2.0}, 20002, 2, 1);
        Project oneSpecies = projectFixture(2);
        oneSpecies.spectra = {binnedSpectrum("e-", {1.0}, {1.0, 2.0}),
                              binnedSpectrum("proton", {1.0}, {1.0, 2.0})};
        oneSpecies.validate();
        rejects([&] { (void)planResponseCases(oneSpecies, ResponseOptions::fromJson(tooManyBatches)); },
                "Response plans above 10000 fixed sub-batches must be rejected");

        const std::string workerSha(64, 'a');
        const Json runtime = {{"engine_version", "fixture-geant4"}, {"dataset_manifest_sha256", std::string(64, 'b')}};
        const Json descriptor = responseDescriptor(project, parsed);
        const Json identity = responseCacheIdentity(project, parsed, workerSha, runtime);
        check(identity.at("descriptor") == descriptor && identity.at("options") == parsed.toJson() &&
              identity.at("worker_sha256") == workerSha && identity.at("runtime_audit") == runtime,
              "Cache identity must bind its normalized descriptor, options, Worker and runtime audit");

        auto irrelevant = project;
        irrelevant.name = "Renamed synthetic project";
        irrelevant.environmentSource = "new source annotation";
        irrelevant.environmentMetadata["model"] = "different metadata only";
        irrelevant.duration = 13.0;
        irrelevant.events = 29;
        irrelevant.provenance["last_loaded_input"] = {{"file_name", "new.json"},
            {"size_bytes", 5}, {"sha256", std::string(64, 'c')}};
        for (auto& spectrum : irrelevant.spectra) {
            for (double& value : spectrum.values) value *= 3.0;
            spectrum.prepare();
            spectrum.provenance["source_file"] = {{"file_name", "renamed.csv"},
                {"size_bytes", 9}, {"sha256", std::string(64, 'd')}};
        }
        irrelevant.validate();
        check(responseDescriptor(irrelevant, parsed) == descriptor &&
              responseCacheIdentity(irrelevant, parsed, workerSha, runtime) == identity,
              "Project name/source metadata, flux magnitude, duration, editor events and provenance must not invalidate a cache");
        auto fluenceEquivalent = project.convertedQuantity(SpectrumQuantity::Fluence);
        check(responseDescriptor(fluenceEquivalent, parsed) == descriptor,
              "Equivalent flux-to-fluence conversion must retain the same unit-fluence response identity");

        auto geometryChanged = project; geometryChanged.volumes[0].size[0] += 1.0; geometryChanged.validate();
        auto materialChanged = project; materialChanged.volumes[0].material = "G4_Al"; materialChanged.validate();
        auto cutChanged = project; cutChanged.cut += 0.01; cutChanged.validate();
        auto radiusChanged = project; radiusChanged.radius += 1.0; radiusChanged.validate();
        auto seedChanged = project; ++seedChanged.seed; seedChanged.validate();
        auto physicsChanged = project; physicsChanged.physics = "QGSP_BIC_EMZ"; physicsChanged.validate();
        check(responseDescriptor(geometryChanged, parsed) != descriptor &&
              responseDescriptor(materialChanged, parsed) != descriptor &&
              responseDescriptor(cutChanged, parsed) != descriptor &&
              responseDescriptor(radiusChanged, parsed) != descriptor &&
              responseDescriptor(seedChanged, parsed) != descriptor &&
              responseDescriptor(physicsChanged, parsed) != descriptor,
              "Geometry, material, production cut, source radius, seed and physics must invalidate the descriptor");
        auto optionsChanged = ResponseOptions::fromJson(optionsJson({"proton", "e-"}, {1.0, 2.0, 4.0}, 8, 4, 2));
        check(responseCacheIdentity(project, optionsChanged, workerSha, runtime) != identity &&
              responseCacheIdentity(project, parsed, std::string(64, 'e'), runtime) != identity &&
              responseCacheIdentity(project, parsed, workerSha, Json{{"engine_version", "other"}}) != identity,
              "Response options, Worker SHA and runtime changes must produce distinct cache identities");

        const auto results = allCaseResults(plan);
        const Json matrix = makeResponseMatrix(project, parsed, results);
        check(matrix.at("rows").size() == plan.size(),
              "Response matrix must contain one row for every planned basis case");
        for (std::size_t i = 0; i < plan.size(); ++i) {
            const auto& row = matrix.at("rows").at(i);
            check(row.at("index") == plan[i].index && row.at("particle") == plan[i].particle &&
                  row.at("low_MeV") == plan[i].low && row.at("high_MeV") == plan[i].high &&
                  row.at("histories") == parsed.historiesPerCase && row.at("targets").size() == 1 &&
                  row.at("targets").at(0).at("id") == "Target_Si" &&
                  row.at("targets").at(0).at("mass_kg") == 0.00233,
                  "Matrix rows must preserve each basis identity, history count and consistent target mass");
        }
        const Json applied = applyResponseMatrix(project, parsed, matrix);
        const auto reference = weightedReference(project, plan, 0.00233);
        const auto& target = applied.at("targets").at(0);
        check(target.at("id") == "Target_Si" && target.at("mass_kg") == 0.00233 &&
              near(target.at("dose_Gy"), reference.doseGy) &&
              near(target.at("total_dose_Gy"), reference.doseGy * 1.5L) &&
              near(target.at("nonionizing_dose_Gy"), reference.doseGy * 0.5L) &&
              near(target.at("standard_error_Gy"), reference.standardErrorGy) &&
              near(target.at("relative_error"), reference.standardErrorGy / reference.doseGy),
              "Independent hand calculation must match weighted dose and quadrature standard error");
        check((target.at("quality") == "unknown" || target.at("quality") == "insufficient" ||
               target.at("quality") == "sufficient"),
              "Applied target must carry a valid statistical quality class");
        const Json fluenceApplied = applyResponseMatrix(fluenceEquivalent, parsed, matrix);
        check(near(fluenceApplied.at("targets").at(0).at("dose_Gy"), reference.doseGy) &&
              near(fluenceApplied.at("targets").at(0).at("standard_error_Gy"), reference.standardErrorGy),
              "Equivalent flux-duration and fluence inputs must yield the same applied response");

        const auto withZeroCase = makeResponseMatrix(project, parsed, allCaseResults(plan, 1));
        auto zeroWeightProject = project;
        zeroWeightProject.spectra[1].values[1] = 0.0;
        zeroWeightProject.spectra[1].prepare();
        zeroWeightProject.validate();
        const Json zeroWeightApplied = applyResponseMatrix(zeroWeightProject, parsed, withZeroCase);
        check(!zeroWeightApplied.at("targets").at(0).at("relative_error").is_null() &&
              zeroWeightApplied.at("targets").at(0).at("dose_Gy").get<double>() > 0.0,
              "A zero-hit basis with zero current weight must not make the combined RSE unknown");
        const Json positiveWeightApplied = applyResponseMatrix(project, parsed, withZeroCase);
        check(positiveWeightApplied.at("targets").at(0).at("relative_error").is_null() &&
              positiveWeightApplied.at("targets").at(0).at("quality") == "unknown",
              "A positive-weight zero-hit basis must keep the combined relative error unknown");

        rejects([&] { (void)makeResponseMatrix(project, parsed, {}); },
                "An empty set of completed response cases must be rejected");
        auto incomplete = results; incomplete.pop_back();
        rejects([&] { (void)makeResponseMatrix(project, parsed, incomplete); },
                "A response matrix missing a planned case must be rejected");
        auto wrongMass = results; wrongMass[1]["targets"][0]["mass_kg"] = 0.003;
        rejects([&] { (void)makeResponseMatrix(project, parsed, wrongMass); },
                "Case target mass disagreement must be rejected");
        auto wrongExposure = results; wrongExposure[0]["expected_primaries"] =
            wrongExposure[0].at("expected_primaries").get<double>() + 1.0;
        rejects([&] { (void)makeResponseMatrix(project, parsed, wrongExposure); },
                "Case primary normalization mismatch must be rejected");
        auto wrongMoments = results; wrongMoments[0].erase("score_moments");
        rejects([&] { (void)makeResponseMatrix(project, parsed, wrongMoments); },
                "Missing raw response moments must be rejected");
        auto wrongCaseProject = results; wrongCaseProject[0]["project"]["production_cut_mm"] = 9.0;
        rejects([&] { (void)makeResponseMatrix(project, parsed, wrongCaseProject); },
                "Basis results must be bound to their planned project input");
        rejects([&] { (void)applyResponseMatrix(geometryChanged, parsed, matrix); },
                "Applying a cached response to changed geometry must be rejected");

        std::cout << "Response matrix contract: " << checks
                  << " checks passed; basis cases, dependencies, weights, moments and invalidation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
