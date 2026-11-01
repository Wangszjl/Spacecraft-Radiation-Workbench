#include "spacerad/ShieldingScan.h"
#include "spacerad/Environment.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using spacerad::Project;
using spacerad::ShieldingScanService;
using spacerad::ShieldingScanSettings;
using spacerad::Json;
int failures = 0;
int checks = 0;

void check(bool ok, const char* message) {
    ++checks;
    std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
    if (!ok) ++failures;
}

bool rejects(const std::function<void()>& fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}

ShieldingScanSettings settings(std::vector<double> thicknesses = {0, .5, 3, 6}) {
    return {"Spacecraft_Al", std::move(thicknesses)};
}

const Json& volume(const Json& project, const std::string& id) {
    for (const auto& v : project.at("volumes")) if (v.at("id") == id) return v;
    throw std::runtime_error("planned shell missing");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace spacerad;

    Project base = Project::demo();
    const Json original = base.toJson();
    const Json plan = planShieldingScan(base, settings());
    const auto& cases = plan.at("cases");
    const std::array<double, 4> thicknesses{0, .5, 3, 6};
    const std::array<double, 3> cavity{594, 494, 394};
    bool geometryOk = cases.size() == thicknesses.size();
    if (geometryOk) for (std::size_t i = 0; i < thicknesses.size(); ++i) {
        const auto& c = cases.at(i);
        const Json project = c.at("project");
        bool shellPresent = false;
        for (const auto& v : project.at("volumes")) if (v.at("id") == "Spacecraft_Al") shellPresent = true;
        if (thicknesses[i] == 0) geometryOk = geometryOk && !shellPresent;
        else {
            const auto& shell = volume(project, "Spacecraft_Al");
            const auto size = shell.at("size_mm").get<std::array<double, 3>>();
            for (int d = 0; d < 3; ++d)
                geometryOk = geometryOk && std::abs(size[d] - (cavity[d] + 2 * thicknesses[i])) < 1e-12;
            geometryOk = geometryOk && std::abs(shell.at("wall_mm").get<double>() - thicknesses[i]) < 1e-12;
        }
    }
    check(geometryOk, "fixed 594x494x394 mm cavity; zero removes shell and positive thickness grows outer box");
    check(base.toJson() == original, "planning leaves the input project unchanged");
    check(plan.at("histories_per_case") == base.events && plan.at("total_planned_histories") == base.events * 4,
          "one species produces one fixed-history case per thickness");

    Project twoSpecies = setManualSpectrum(base, base.spectra.size(), "e-", {1, 3}, {2, 4});
    const Json twoPlan = planShieldingScan(twoSpecies, settings());
    bool speciesOk = twoPlan.at("cases").size() == 8;
    std::set<std::int64_t> seeds;
    for (const auto& c : twoPlan.at("cases")) {
        const auto& p = c.at("project");
        speciesOk = speciesOk && p.at("environment").at("spectra").size() == 1 &&
            p.at("events") == twoSpecies.events && p.at("source_radius_mm") == twoSpecies.radius &&
            p.at("duration_s") == twoSpecies.duration;
        seeds.insert(p.at("seed").get<std::int64_t>());
    }
    check(speciesOk, "adding electrons doubles cases; each snapshot has one species and preserves histories/source settings");
    check(seeds.size() == 8, "each planned case receives a distinct seed");

    const std::vector<std::vector<double>> badThicknesses{
        {0, 3, 2}, {-1, 1}, {0, 1, 1}, {}, {1},
        [] { std::vector<double> v(65); for (std::size_t i=0;i<v.size();++i) v[i]=double(i); return v; }(),
        {0, 10000.1}
    };
    bool badSettingsRejected = true;
    for (const auto& values : badThicknesses)
        badSettingsRejected = badSettingsRejected && rejects([&] { (void)planShieldingScan(base, settings(values)); });
    check(badSettingsRejected, "unsorted, negative, duplicate, empty, singleton, >64, and >10000 mm settings are rejected");

    Project solid = base;
    for (auto& v : solid.volumes) if (v.id == "Spacecraft_Al") v.wall = 0;
    Project sensitive = base;
    for (auto& v : sensitive.volumes) if (v.id == "Spacecraft_Al") v.sensitive = true;
    check(rejects([&] { (void)planShieldingScan(solid, settings()); }) &&
          rejects([&] { (void)planShieldingScan(sensitive, settings()); }),
          "solid and sensitive selected shells are rejected");
    check(rejects([&] { (void)planShieldingScan(base, {"missing-shell", {0, 1}}); }),
          "unknown shell is rejected");
    check(rejects([&] { (void)planShieldingScan(base, settings({0, 10000})); }),
          "geometry expansion that violates project geometry validation is rejected");

    // With valid Projects, settings cap at 64 thicknesses, supported particles at two,
    // and this service fixes each case to exactly one batch (maxHistories == events).
    // Therefore the 10,000 planned-batch ceiling cannot be crossed by a valid input.
    Project maxShape = twoSpecies;
    maxShape.radius = 1000;
    const Json maxShapePlan = planShieldingScan(maxShape, settings([] {
        std::vector<double> v(64); for (std::size_t i=0;i<v.size();++i) v[i]=double(i)/10; return v;
    }()));
    check(maxShapePlan.at("cases").size() == 128,
          "validated inputs stay below the 10,000 batch ceiling (maximum is 64 thicknesses x 2 species)");

    ShieldingScanService service;
    QTemporaryDir temp;
    const QString missingRuntime = temp.filePath("must-not-create");
    bool runtimeRejected = rejects([&] { service.start(base, settings(), missingRuntime); });
    check(runtimeRejected && !QFileInfo::exists(missingRuntime),
          "start without configured runtime rejects before creating its output directory");
    const QString missingArchive = temp.filePath("missing-archive");
    check(rejects([&] { (void)loadShieldingScan(missingArchive); }),
          "offline loading rejects a missing/nonterminal scan archive");

    std::cout << "Checks complete: " << checks << " checks; " << failures << " failure(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
