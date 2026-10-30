#include <spacerad/Environment.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace spacerad;

namespace {
bool close(double a, double b) {
    return std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)});
}
}

int main() {
    int checks = 0;
    auto check = [&](bool ok, const char* why) {
        ++checks;
        if (!ok) throw std::runtime_error(why);
    };
    auto rejects = [&](auto operation, const char* why) {
        bool rejected = false;
        try { operation(); } catch (const std::exception&) { rejected = true; }
        check(rejected, why);
    };

    try {
        auto project = Project::demo();
        project.environmentMetadata["model"] = "Orbit-derived fixture";
        const auto originalJson = project.toJson();
        const auto originalCount = project.spectra.size();
        auto edited = setManualSpectrum(project, originalCount, "e-", {1, 3}, {2, 4});

        check(project.toJson() == originalJson, "manual spectrum edit mutated its input project");
        check(edited.spectra.size() == originalCount + 1, "index==size did not append a spectrum");
        check(edited.spectra.back().particle == "e-", "appended particle is wrong");
        check(edited.spectra.back().interpolation == "linear", "default interpolation is not linear");
        check(close(edited.spectra.back().integral, 6), "linear trapezoid integral is wrong");
        check(edited.synthetic, "manual edit did not mark the environment synthetic");
        check(edited.environmentMetadata == spacerad::engine::defaultEnvironmentMetadata(), "manual edit retained hidden orbit metadata");
        check(edited.environmentSource.find("SYNTHETIC") != std::string::npos &&
              edited.environmentSource.find("no orbit model") != std::string::npos,
              "synthetic source does not explain that no orbit model was used");
        const auto& manualProvenance = edited.spectra.back().provenance;
        check(manualProvenance.at("source_file").is_null(), "manual spectrum has a source file");
        check(manualProvenance.at("conversions").empty(), "manual spectrum has conversion history");
        check(manualProvenance.at("manual_definition").at("schema") == "spacerad.manual-spectrum/1",
              "manual definition schema missing");
        check(manualProvenance.at("manual_definition").at("quantity") ==
              spacerad::engine::quantityName(edited.quantity), "manual quantity provenance is wrong");

        auto replaced = setManualSpectrum(edited, originalCount, "e-", {1, 2, 4}, {3, 5}, "bin_constant");
        check(replaced.spectra.size() == edited.spectra.size(), "replacement added a spectrum instead of replacing");
        check(replaced.spectra.back().interpolation == "bin_constant", "bin_constant interpolation was not retained");
        check(close(replaced.spectra.back().integral, 13), "bin_constant bin-width integral is wrong");
        check(edited.spectra.back().energy == std::vector<double>({1, 3}) &&
              edited.spectra.back().values == std::vector<double>({2, 4}), "replacement mutated its input");

        auto other = setManualSpectrum(replaced, replaced.spectra.size(), "gamma", {2, 5}, {1, 1});
        check(other.spectra.size() == replaced.spectra.size() + 1, "distinct particle append failed");
        check(other.spectra[0].particle == project.spectra[0].particle &&
              other.spectra[0].energy == project.spectra[0].energy &&
              other.spectra[0].values == project.spectra[0].values &&
              other.spectra[0].provenance == project.spectra[0].provenance,
              "untouched spectrum content or provenance changed");

        rejects([&] { setManualSpectrum(other, other.spectra.size() + 1, "alpha", {1, 2}, {1, 1}); }, "out-of-range insertion accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "e-", {1, 2}, {1, 1}); }, "duplicate particle append accepted");
        rejects([&] { setManualSpectrum(other, 0, "e-", {1, 2}, {1, 1}); }, "duplicate particle replacement accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {2, 1}, {1, 1}); }, "descending energy grid accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 1}, {1, 1}); }, "repeated energy accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 2}, {-1, 1}); }, "negative differential value accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 2}, {0, 0}); }, "all-zero spectrum accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 2}, {1, std::numeric_limits<double>::quiet_NaN()}); }, "NaN value accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 2}, {1}); }, "mismatched energy and value counts accepted");
        rejects([&] { setManualSpectrum(other, other.spectra.size(), "alpha", {1, 2}, {1, 1}, "cubic"); }, "unsupported interpolation accepted");

        auto removed = removeSpectrum(other, other.spectra.size() - 1);
        check(removed.spectra.size() + 1 == other.spectra.size(), "removeSpectrum did not remove one spectrum");
        check(other.spectra.size() == removed.spectra.size() + 1, "removeSpectrum mutated its input");
        rejects([&] { removeSpectrum(other, other.spectra.size()); }, "out-of-range removal accepted");
        rejects([&] { removeSpectrum(Project{}, 0); }, "removal from empty environment accepted");
        auto sole = other;
        sole.spectra.resize(1);
        sole.validate();
        rejects([&] { removeSpectrum(sole, 0); }, "last spectrum removal accepted");

        auto fluence = other.convertedQuantity(spacerad::engine::SpectrumQuantity::Fluence);
        check(close(fluence.expectedPrimaries(), other.expectedPrimaries()), "flux-to-fluence changed expected primaries");
        check(close(fluence.spectra[0].values[0], other.spectra[0].values[0] * other.duration),
              "flux-to-fluence did not scale values by duration");
        check(close(fluence.spectra[0].integral, other.spectra[0].integral * other.duration),
              "flux-to-fluence did not scale integral by duration");
        auto fluxAgain = fluence.convertedQuantity(spacerad::engine::SpectrumQuantity::Flux);
        bool valuesRestored = fluxAgain.spectra[0].values.size() == other.spectra[0].values.size();
        for (std::size_t i = 0; valuesRestored && i < other.spectra[0].values.size(); ++i)
            valuesRestored = close(fluxAgain.spectra[0].values[i], other.spectra[0].values[i]);
        check(valuesRestored, "fluence-to-flux did not restore values");
        check(fluxAgain.spectra[0].provenance.at("conversions").size() >= 2,
              "quantity conversion history was lost");

        auto reopened = Project::fromJson(other.toJson());
        check(reopened.spectra.back().provenance == other.spectra.back().provenance,
              "serialized manual definition did not survive reload");
        check(reopened.spectra.back().energy == other.spectra.back().energy &&
              reopened.spectra.back().values == other.spectra.back().values,
              "serialized manual spectrum did not survive reload");
        check(reopened.environmentMetadata == spacerad::engine::defaultEnvironmentMetadata(),
              "default metadata changed after manual spectrum roundtrip");

        std::cout << checks << " environment authoring checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
