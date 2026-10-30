#include "RadiationProject.h"
#include "runtime/EnvironmentImport.h"
#include "runtime/ProjectFile.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace spacerad::engine;

namespace {
int checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

bool close(double actual, double expected, double tolerance = 1e-11) {
    return std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected));
}

template<class F>
void rejects(F operation, const char* message = "Invalid SPENVIS/import input was accepted") {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

QByteArray vectorFixture() {
    // Synthetic parser fixture only: schema counts, metadata and values are
    // hand-authored to exercise the documented SPENVIS tabular container.
    return QByteArray(
        "'*', 7, 1, 2, 1, 2, 5, 2, 0\r\n"
        "'SYNTHETIC fixture; O''Brien comment, not an orbit result'\r\n"
        "'ENERGY', 4, 1.0D+00, 2.0D+00, 4.0D+00, 8.0D+00\r\n"
        "'MODEL', -1, 'SYNTHETIC O''Brien fixture'\r\n"
        "################ reserved header area ################\r\n"
        "'FLUX_PR', 'cm-2 s-1', 4, 'Integral proton flux'\r\n"
        "'AUX', '1', 1, 'Synthetic auxiliary value'\r\n"
        "1.0D+02, 8.0D+01, 2.0D+01, 5.0D+00, 7.0D+00\r\n"
        "2.0D+02, 1.6D+02, 4.0D+01, 1.0D+01, 8.0D+00\r\n"
        "'End of Block'\r\n");
}

QByteArray scalarFixture() {
    return QByteArray(
        "'*', 4, 0, 1, 0, 2, 2, 4, 0\r\n"
        "'MODEL', -1, 'SYNTHETIC scalar fixture'\r\n"
        "'ENERGY', 'MeV', 1, 'Particle energy'\r\n"
        "'FLUX_PR', 'cm-2 s-1', 1, 'Integral proton flux'\r\n"
        "1.0D+00, 1.0D+02\r\n"
        "2.0D+00, 8.0D+01\r\n"
        "4.0D+00, 2.0D+01\r\n"
        "8.0D+00, 5.0D+00\r\n"
        "'End of File'\r\n");
}

Json vectorOptions(int row = 0) {
    return {{"schema", "scdat.spenvis-import/1"}, {"block", 0}, {"row", row},
            {"variable", "FLUX_PR"}, {"assume_isotropic", true},
            {"energy_unit", "MeV_per_particle"}};
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read temporary test file");
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write temporary test file");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const std::vector<double> energy{1, 2, 4, 8};
        const std::vector<double> above{100, 80, 20, 5};
        auto spectrum = integralSpectrum(energy, above, "proton", false, SpectrumQuantity::Flux);
        check(spectrum.interpolation == "bin_constant", "Integral spectrum must use constant bins");
        check(spectrum.energy == energy, "Integral thresholds must be retained as bin edges");
        check(spectrum.values.size() == 3 && close(spectrum.values[0], 20)
              && close(spectrum.values[1], 30) && close(spectrum.values[2], 3.75),
              "Above-threshold differences produced wrong bin values");
        check(close(spectrum.integral, 95), "Retained spectrum integral must equal 95");
        check(close(spectrum.provenance.at("conversions").back().at("excluded_above_last_threshold").get<double>(), 5),
              "Excluded tail above the final retained threshold must equal 5");
        check(close(spectrum.sample(0.5), 2.0 + 27.5 / 30.0), "Constant-bin median sample is wrong");
        check(spectrum.sample(0) == 1, "Zero quantile must select the first edge");

        auto withZeroBin = integralSpectrum({1, 2, 4, 8}, {100, 80, 80, 0}, "proton", false,
                                            SpectrumQuantity::Flux);
        check(withZeroBin.values.size() == 3 && withZeroBin.values[1] == 0,
              "Fixture should contain a zero-density bin");
        check(close(withZeroBin.sample(0.5), 5.5), "Sampling must skip a zero-density bin safely");
        auto leadingZero=integralSpectrum({1,2,4},{100,100,0},"proton",false,SpectrumQuantity::Flux);
        check(leadingZero.sample(0)==2,"Leading zero bin must not produce a NaN or a spurious low-energy sample");
        auto trailingZero=integralSpectrum({1,2,4},{100,0,0},"proton",false,SpectrumQuantity::Flux);
        check(std::isfinite(trailingZero.sample(std::nextafter(1.0,0.0)))&&trailingZero.sample(std::nextafter(1.0,0.0))<=2,"Trailing zero bin sampling failed");
        rejects([&]{integralSpectrum(energy,{100,80,81,5},"proton",false,SpectrumQuantity::Flux);},"Increasing above-threshold values accepted");
        auto angular=integralSpectrum(energy,above,"proton",true,SpectrumQuantity::Fluence);
        check(close(angular.integral,380*pi),"Integral per-sr conversion must apply 4*pi once");

        rejects([&] { integralSpectrum({1, 4, 2, 8}, above, "proton", false, SpectrumQuantity::Flux); }, "Nonmonotone thresholds accepted");
        rejects([&] { integralSpectrum({1, 2, 4, 8}, {100, 80, -1, 5}, "proton", false, SpectrumQuantity::Flux); }, "Negative integral spectrum accepted");
        rejects([&] { integralSpectrum({1, 2, 2, 8}, above, "proton", false, SpectrumQuantity::Flux); }, "Repeated thresholds accepted");
        rejects([&] { integralSpectrum({1, 2, 4, 8}, {5, 5, 5, 5}, "proton", false, SpectrumQuantity::Flux); }, "Empty retained spectrum accepted");
        rejects([&] { integralSpectrum({1, 2, std::numeric_limits<double>::infinity(), 8}, above, "proton", false, SpectrumQuantity::Flux); }, "Infinite threshold accepted");
        rejects([&] { integralSpectrum({1, 2, 4, 8}, {100, 80, std::numeric_limits<double>::quiet_NaN(), 5}, "proton", false, SpectrumQuantity::Flux); }, "Nonfinite integral value accepted");

        Project project = Project::demo();
        project.spectra = {spectrum};
        project.validate();
        auto serialized = project.toJson();
        auto roundtrip = Project::fromJson(serialized);
        check(roundtrip.spectra.size() == 1 && roundtrip.spectra[0].interpolation == "bin_constant",
              "Schema v2 lost bin_constant interpolation");
        check(roundtrip.spectra[0].energy == energy && roundtrip.spectra[0].values == spectrum.values,
              "Schema v2 changed constant-bin edges or values");
        auto fluence = project.convertedQuantity(SpectrumQuantity::Fluence);
        check(fluence.spectra[0].interpolation == "bin_constant" && fluence.spectra[0].energy == energy,
              "Flux-to-fluence conversion changed bin semantics");
        for (size_t i = 0; i < spectrum.values.size(); ++i)
            check(close(fluence.spectra[0].values[i], spectrum.values[i] * project.duration),
                  "Flux-to-fluence conversion did not scale bin values");
        auto fluxAgain = fluence.convertedQuantity(SpectrumQuantity::Flux);
        check(fluxAgain.spectra[0].interpolation == "bin_constant" && fluxAgain.spectra[0].energy == energy,
              "Fluence-to-flux conversion changed bin semantics");
        for (size_t i = 0; i < spectrum.values.size(); ++i)
            check(close(fluxAgain.spectra[0].values[i], spectrum.values[i]), "Bin conversion roundtrip drift");

        const QByteArray vector = vectorFixture();
        auto inspected = inspectSpenvis(vector);
        check(inspected.size() == 1 && inspected[0].at("block") == 0 && inspected[0].at("rows") == 2,
              "SPENVIS inspect block/row counts are wrong");
        check(inspected[0].at("metadata").at("MODEL") == "SYNTHETIC O'Brien fixture",
              "Escaped apostrophe metadata did not roundtrip");
        check(inspected[0].at("variables").size() == 2, "SPENVIS variable descriptions missing");

        auto imported = parseSpenvis(vector, vectorOptions());
        check(imported.spectrum.particle == "proton" && imported.quantity == SpectrumQuantity::Flux,
              "SPENVIS vector selection mapped to wrong particle or quantity");
        check(imported.spectrum.interpolation == "bin_constant" && imported.spectrum.values.size() == 3,
              "SPENVIS integral variable did not become constant bins");
        check(close(imported.spectrum.integral, 95), "SPENVIS selected vector retained integral mismatch");
        check(imported.metadata.at("model") == "SYNTHETIC O'Brien fixture", "Model metadata not mapped");
        check(imported.mapping.at("selection") == "single vector row; not an orbit/time average",
              "Vector row selection must remain explicit");
        auto secondRow = parseSpenvis(vector, vectorOptions(1));
        check(close(secondRow.spectrum.integral, 190), "Explicit row 1 selection was not honored");
        check(imported.mapping.at("row") == 0 && secondRow.mapping.at("row") == 1,
              "Selected row identity was not preserved");

        auto scalar = parseSpenvis(scalarFixture(), vectorOptions());
        check(scalar.mapping.at("selection") == "all energy rows in scalar-column block",
              "Scalar spectrum should use all energy rows");
        check(scalar.spectrum.energy == energy && close(scalar.spectrum.integral, 95),
              "Scalar ENERGY/FLUX_PR fixture was not parsed correctly");

        rejects([&] { auto options = vectorOptions(); options["schema"] = "scdat.spenvis-import/9"; parseSpenvis(vector, options); }, "Unsupported options schema accepted");
        rejects([&] { auto options = vectorOptions(); options["assume_isotropic"] = false; parseSpenvis(vector, options); }, "Implicit anisotropy accepted");
        rejects([&] { auto options = vectorOptions(); options["row"] = 2; parseSpenvis(vector, options); }, "Out-of-range row accepted");
        rejects([&] { auto options = vectorOptions(); options["energy_unit"]="keV"; parseSpenvis(vector, options); }, "Wrong declared energy unit accepted");
        rejects([&] {
            QByteArray mixed="'*',4,0,0,0,3,3,2,0\n'ENERGY','MeV',1,'Energy'\n'FLUX_PR','cm-2 s-1',1,'Integral proton flux'\n'TIME','s',1,'Time'\n1,100,0\n2,80,1\n'End of File'\n";
            parseSpenvis(mixed,vectorOptions());
        }, "Mixed time/energy samples incorrectly merged into a single spectrum");
        rejects([&] { auto bad = vector; bad.replace("'cm-2 s-1'", "'MeV' "); parseSpenvis(bad, vectorOptions()); }, "Unsupported units accepted");
        rejects([&] { auto bad = vector; bad.replace("1.0D+02, 8.0D+01, 2.0D+01, 5.0D+00, 7.0D+00", "1.0D+02, 8.0D+01, 2.0D+01, 5.0D+00"); parseSpenvis(bad, vectorOptions()); }, "Wrong body column count accepted");
        rejects([&] { auto bad = vector; bad.replace("'*', 7, 1, 2, 1, 2, 5, 2, 0", "'*', 6, 1, 2, 1, 2, 5, 2, 0"); parseSpenvis(bad, vectorOptions()); }, "Inconsistent header counts accepted");
        rejects([&] { auto bad = vector; bad.replace("'MODEL', -1, 'SYNTHETIC O''Brien fixture'", "'ENERGY', -1, 'duplicate key'"); parseSpenvis(bad, vectorOptions()); }, "Duplicate metadata key accepted");
        rejects([&] { auto bad = vector; bad.replace("'End of Block'\r\n", ""); parseSpenvis(bad, vectorOptions()); }, "Missing footer accepted");

        QTemporaryDir temporary;
        check(temporary.isValid(), "Cannot create temporary import directory");
        const QString sourcePath = temporary.filePath("synthetic-spenvis.txt");
        const QString projectPath = temporary.filePath("imported-project.json");
        writeFile(sourcePath, vector);
        rejects([&]{auto opt=vectorOptions();opt["expected_sha256"]=std::string(64,'0');loadSpenvis(sourcePath,opt);},"Changed preview source accepted");
        const auto csvPath=temporary.filePath("integral.csv");
        writeFile(csvPath,"energy_MeV,integral_fluence\n1,100\n2,80\n4,20\n8,5\n");
        auto csvSpectrum=loadSpectrumCsv(csvPath,"proton",true,SpectrumQuantity::Fluence,true);
        check(close(csvSpectrum.integral,380*pi)&&csvSpectrum.provenance.at("conversions").size()==2,"CSV integral conversion duplicated 4*pi");
        check(csvSpectrum.provenance.at("original_quantity")=="integral_fluence","CSV integral provenance wrong");
        rejects([&]{loadSpectrumCsv(csvPath,"proton",false,SpectrumQuantity::Flux,true);},"Mismatched integral quantity header accepted");
        writeFile(csvPath,"energy_MeV,differential_flux\n1,100\n2,80\n4,20\n8,5\n");
        rejects([&]{loadSpectrumCsv(csvPath,"proton",false,SpectrumQuantity::Flux,true);},"Differential header incorrectly treated as integral");
        auto loaded = loadSpenvis(sourcePath, vectorOptions());
        check(loaded.spectrum.provenance.at("source_file").at("bytes_base64").is_string(),
              "Loaded source file bytes were not embedded as base64");
        auto applied = applyEnvironmentImport(Project::demo(), loaded, true);
        check(applied.synthetic && applied.spectra.size() == 1, "Synthetic fixture state was not retained");
        saveProjectFile(projectPath, applied);
        auto reopened = loadProjectFile(projectPath);
        const auto& sourceIdentity = reopened.project.spectra[0].provenance.at("source_file");
        check(sourceIdentity.at("sha256").is_string() && sourceIdentity.at("bytes_base64").is_string(),
              "Reopening lost embedded raw-source identity");
        check(reopened.project.spectra[0].interpolation == "bin_constant",
              "Reopening imported project lost constant-bin spectrum");
        auto tamperedJson = reopened.project.toJson();
        auto encoded = tamperedJson["environment"]["spectra"][0]["provenance"]["source_file"]["bytes_base64"].get<std::string>();
        encoded[0] = encoded[0] == 'A' ? 'B' : 'A';
        tamperedJson["environment"]["spectra"][0]["provenance"]["source_file"]["bytes_base64"] = encoded;
        const QString tamperedPath = temporary.filePath("tampered-project.json");
        writeFile(tamperedPath, QByteArray::fromStdString(tamperedJson.dump(2)));
        rejects([&] { loadProjectFile(tamperedPath); }, "Tampered embedded original source bytes accepted");

        std::cout << "Radiation import contract: " << checks
                  << " checks passed; integral bins, SPENVIS format parsing, schema roundtrip and source identity\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
