#include "RadiationProject.h"
#include "runtime/BatchStatistics.h"
#include <QCoreApplication>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

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
bool near(double actual, long double expected, long double rel = 2e-12L, long double abs = 1e-20L) {
    const long double a = actual;
    return std::abs(a - expected) <= abs + rel * std::abs(expected);
}
Project fixtureProject(int events = 8) {
    Project p = Project::demo();
    p.name = "Synthetic batch statistics fixture";
    p.volumes = {{"Target_Si", "G4_Si", {0, 0, 0}, {20, 20, 20}, 0, true}};
    p.assemblies.clear(); p.templates.clear(); p.instances.clear();
    p.events = events;
    p.seed = 2147483645;
    p.validate();
    return p;
}
struct ReferenceMoments { long double mean = 0, m2 = 0, se = 0; int nonzero = 0; };
ReferenceMoments referenceMoments(const std::vector<double>& xs) {
    ReferenceMoments out;
    long double sum = 0;
    for (double x : xs) { sum += static_cast<long double>(x); if (x != 0) ++out.nonzero; }
    out.mean = sum / static_cast<long double>(xs.size());
    for (double x : xs) {
        const long double delta = static_cast<long double>(x) - out.mean;
        out.m2 += delta * delta;
    }
    out.se = std::sqrt(out.m2 / (static_cast<long double>(xs.size()) * (xs.size() - 1)));
    return out;
}
Json eventMoments(const std::vector<double>& xs) {
    const auto ref = referenceMoments(xs);
    return {{"count", xs.size()}, {"nonzero_histories", ref.nonzero},
            {"mean_MeV", static_cast<double>(ref.mean)}, {"m2_MeV2", static_cast<double>(ref.m2)}};
}
Json makeResult(const Project& base, int seed, const std::vector<double>& ionizing,
                const std::vector<double>& total, const std::vector<double>& nonionizing,
                int entries = 1) {
    Project child = base;
    child.events = static_cast<int>(ionizing.size());
    child.seed = seed;
    const double mass = 0.005;
    const Json stableGeometry = {{"contract", "fixture-geometry"}, {"volumes", Json::array({{{"id", "Target_Si"}}})}};
    const Json stableMaterials = {{"status", "pinned_match"}, {"snapshot_sha256", std::string(64, 'a')}};
    const Json stableRuntime = {{"engine_version", "fixture"}, {"datasets", Json::array({"fixture-data"})}};
    return {{"schema", "scdat.radiation-result/2"}, {"status", "completed"},
            {"project", child.toJson()}, {"geometry_audit", stableGeometry},
            {"material_audit", stableMaterials}, {"runtime_audit", stableRuntime},
            {"expected_primaries", base.expectedPrimaries()}, {"histories", child.events},
            {"track_segments", Json::array({{{"event", 0}, {"points_mm", Json::array({{0, 0, 0}, {1, 0, 0}})}}})},
            {"targets", Json::array({{{"id", "Target_Si"}, {"mass_kg", mass},
                                       {"primary_entry_histories", entries}}})},
            {"score_moments", {{"Target_Si", {{"ionizing", eventMoments(ionizing)},
                                                {"total", eventMoments(total)},
                                                {"nonionizing", eventMoments(nonionizing)}}}}}};
}
BatchOptions testOptions() {
    BatchOptions options;
    options.historiesPerBatch = 4; options.maxHistories = 10; options.maxParallel = 2;
    options.maxSeconds = 0; options.targetRelativeError = 0.1;
    options.minNonzeroHistories = 30; options.minCompletedBatches = 2;
    return options;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const Project project = fixtureProject();
        const std::vector<double> ionA{0, 1, 2};
        const std::vector<double> ionB{0, 0, 3, 4, 5};
        const std::vector<double> totalA{0, 1.5, 2.5};
        const std::vector<double> totalB{0, 0, 3.5, 4.5, 5.5};
        const std::vector<double> nielA{0, 0.5, 1};
        const std::vector<double> nielB{0, 0, 1.5, 2, 2.5};
        auto first = makeResult(project, 101, ionA, totalA, nielA, 2);
        auto second = makeResult(project, 102, ionB, totalB, nielB, 3);
        const auto merged = mergeBatchResults(project, {first, second});

        const std::vector<double> ion{0, 1, 2, 0, 0, 3, 4, 5};
        const std::vector<double> total{0, 1.5, 2.5, 0, 0, 3.5, 4.5, 5.5};
        const std::vector<double> niel{0, 0.5, 1, 0, 0, 1.5, 2, 2.5};
        const auto ionRef = referenceMoments(ion);
        const auto totalRef = referenceMoments(total);
        const auto nielRef = referenceMoments(niel);
        const auto& mergedIon = merged.at("score_moments").at("Target_Si").at("ionizing");
        const auto& mergedTotal = merged.at("score_moments").at("Target_Si").at("total");
        const auto& mergedNiel = merged.at("score_moments").at("Target_Si").at("nonionizing");
        check(merged.at("histories") == 8 && merged.at("targets").at(0).at("nonzero_histories") == ionRef.nonzero,
              "Unequal batches must pool all histories, including zero-deposit events");
        check(mergedIon.at("count") == 8 && mergedIon.at("nonzero_histories") == ionRef.nonzero &&
              near(mergedIon.at("mean_MeV"), ionRef.mean) && near(mergedIon.at("m2_MeV2"), ionRef.m2),
              "Pooled ionizing moments must match independent long-double event calculation");
        check(near(mergedTotal.at("mean_MeV"), totalRef.mean) && near(mergedTotal.at("m2_MeV2"), totalRef.m2) &&
              near(mergedNiel.at("mean_MeV"), nielRef.mean) && near(mergedNiel.at("m2_MeV2"), nielRef.m2),
              "Pooled total and nonionizing moments must match independent long-double calculation");
        check(near(mergedIon.at("mean_MeV"), ionRef.mean) && near(mergedIon.at("m2_MeV2"), ionRef.m2) &&
              near(merged.at("targets").at(0).at("tid_standard_error_Gy"),
                   ionRef.se * 1.602176634e-13L / 0.005L * project.expectedPrimaries()),
              "Unequal-n Chan merge must preserve population moments and standard error");
        check(merged.at("expected_primaries") == project.expectedPrimaries() &&
              near(merged.at("targets").at(0).at("tid_Gy"),
                   ionRef.mean * 1.602176634e-13L / 0.005L * project.expectedPrimaries()) &&
              near(merged.at("targets").at(0).at("total_absorbed_Gy"),
                   totalRef.mean * 1.602176634e-13L / 0.005L * project.expectedPrimaries()) &&
              near(merged.at("targets").at(0).at("nonionizing_Gy"),
                   nielRef.mean * 1.602176634e-13L / 0.005L * project.expectedPrimaries()),
              "Exposure and mass conversion must be applied once after pooled means");
        check(merged.at("targets").at(0).at("primary_entry_histories") == 5 &&
              merged.at("track_segments").size() == 1 && merged.at("track_segments").at(0).at("batch_index") == 0,
              "Source entry counts and bounded tracks must be retained with batch identity");

        auto zeroA = makeResult(project, 201, {0, 0, 0}, {0, 0, 0}, {0, 0, 0});
        auto zeroB = makeResult(project, 202, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0});
        const auto zeroMerged = mergeBatchResults(project, {zeroA, zeroB});
        check(zeroMerged.at("score_moments").at("Target_Si").at("ionizing").at("count") == 8 &&
              zeroMerged.at("score_moments").at("Target_Si").at("ionizing").at("nonzero_histories") == 0 &&
              zeroMerged.at("targets").at(0).at("relative_standard_error").is_null(),
              "Zero-hit pooled run must retain event count and report null relative error");
        check(!batchConvergence(zeroMerged, testOptions(), 4).at("converged").get<bool>(),
              "Zero-hit run must never claim statistical convergence");
        check(!batchConvergence(merged, testOptions(), 2).at("converged").get<bool>(),
              "Insufficient nonzero histories must prevent convergence");
        auto enough = merged;
        enough["targets"][0]["nonzero_histories"] = 30;
        enough["targets"][0]["relative_standard_error"] = 0.05;
        check(!batchConvergence(enough, testOptions(), 1).at("converged").get<bool>(),
              "Insufficient completed batches must prevent convergence");
        check(batchConvergence(enough, testOptions(), 2).at("converged").get<bool>(),
              "Sufficient nonzero histories, batch count and relative error must converge");
        enough["targets"][0]["nonzero_histories"] = 29;
        check(!batchConvergence(enough, testOptions(), 3).at("converged").get<bool>(),
              "Falling below the nonzero-history minimum must reopen convergence");

        auto options = testOptions();
        options.maxHistories = 10; options.historiesPerBatch = 3;
        auto planProject = fixtureProject(10); planProject.seed = 2147483646; planProject.validate();
        const auto plan = planBatches(planProject, options);
        const auto planJson = batchPlanJson(plan);
        check(plan.size() == 3 && plan[0].histories == 3 && plan[1].histories == 3 && plan[2].histories == 4,
              "A final one-event remainder must be joined to the preceding batch");
        check(plan[0].seed == 2147483646 && plan[1].seed == 1 && plan[2].seed == 2,
              "Batch seed sequence must wrap at the valid maximum without reusing a seed");
        check(planJson.at(0).at("event_begin") == 0 && planJson.at(0).at("event_end_exclusive") == 3 &&
              planJson.at(1).at("event_begin") == 3 && planJson.at(1).at("event_end_exclusive") == 6 &&
              planJson.at(2).at("event_begin") == 6 && planJson.at(2).at("event_end_exclusive") == 10 &&
              planJson.at(2).at("histories") == 4,
              "Batch event intervals must be contiguous, exclusive-ended and cover the budget exactly");
        check(std::set<int>{plan[0].seed, plan[1].seed, plan[2].seed}.size() == plan.size(),
              "Planned seeds must be unique within the job");

        auto optionsJson = options.toJson();
        check(BatchOptions::fromJson(optionsJson).toJson() == optionsJson,
              "Batch options must round-trip through the strict schema");
        auto unknown = optionsJson; unknown["unrecognized"] = 1;
        rejects([&] { (void)BatchOptions::fromJson(unknown); }, "Unknown batch option must be rejected");
        auto negative = optionsJson; negative["max_seconds"] = -1.0;
        rejects([&] { (void)BatchOptions::fromJson(negative); }, "Negative runtime budget must be rejected");
        auto invalidCount = optionsJson; invalidCount["histories_per_batch"] = 1;
        rejects([&] { (void)BatchOptions::fromJson(invalidCount); }, "Batch size below variance minimum must be rejected");
        auto invalidSchema = optionsJson; invalidSchema["schema"] = "scdat.radiation-batch/2";
        rejects([&] { (void)BatchOptions::fromJson(invalidSchema); }, "Unknown batch schema must be rejected");
        auto invalidMinimum = optionsJson; invalidMinimum["min_completed_batches"] = 1;
        rejects([&] { (void)BatchOptions::fromJson(invalidMinimum); }, "Invalid minimum completed-batch count must be rejected");

        auto mismatchedPlanProject = planProject; mismatchedPlanProject.events = 11; mismatchedPlanProject.validate();
        rejects([&] { (void)planBatches(mismatchedPlanProject, options); },
                "Project event budget must match batch max_histories");
        rejects([&] { (void)mergeBatchResults(project, {}); }, "Empty completed-result set must be rejected");

        auto duplicateSeed = second;
        duplicateSeed["project"]["seed"] = first.at("project").at("seed");
        rejects([&] { (void)mergeBatchResults(project, {first, duplicateSeed}); }, "Duplicate batch seed must be rejected");
        auto mismatchedEvents = second;
        mismatchedEvents["project"]["events"] = 4;
        rejects([&] { (void)mergeBatchResults(project, {first, mismatchedEvents}); },
                "Batch project events must equal that batch's completed histories");
        auto missionMismatch = second;
        missionMismatch["project"]["duration_s"] = missionMismatch.at("project").at("duration_s").get<double>() + 1;
        rejects([&] { (void)mergeBatchResults(project, {first, missionMismatch}); },
                "Batch mission configuration mismatch must be rejected");
        auto runtimeMismatch = second; runtimeMismatch["runtime_audit"]["engine_version"] = "other";
        rejects([&] { (void)mergeBatchResults(project, {first, runtimeMismatch}); },
                "Runtime audit mismatch must be rejected");
        auto materialMismatch = second; materialMismatch["material_audit"]["snapshot_sha256"] = std::string(64, 'b');
        rejects([&] { (void)mergeBatchResults(project, {first, materialMismatch}); },
                "Material audit mismatch must be rejected");
        auto massMismatch = second; massMismatch["targets"][0]["mass_kg"] = 0.006;
        rejects([&] { (void)mergeBatchResults(project, {first, massMismatch}); },
                "Scoring target mass mismatch must be rejected");
        auto exposureMismatch = second; exposureMismatch["expected_primaries"] = project.expectedPrimaries() + 1;
        rejects([&] { (void)mergeBatchResults(project, {first, exposureMismatch}); },
                "Batch exposure normalization mismatch must be rejected");
        auto missingMoments = second; missingMoments.erase("score_moments");
        rejects([&] { (void)mergeBatchResults(project, {first, missingMoments}); },
                "Missing event moments must be rejected");
        auto failed = second; failed["status"] = "failed";
        rejects([&] { (void)mergeBatchResults(project, {first, failed}); },
                "Incomplete batch result must be rejected");

        std::cout << "Batch statistics contract: " << checks
                  << " checks passed; independent moments, event pooling, planning, stopping and rejection rules\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
