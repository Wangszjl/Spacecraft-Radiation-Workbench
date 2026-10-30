#include "spacerad/Environment.h"
#include <stdexcept>

namespace spacerad {
namespace {
void markManual(Project& project) {
    project.synthetic = true;
    project.environmentSource = "SYNTHETIC manually specified differential environment; no orbit model executed";
    project.environmentMetadata = engine::defaultEnvironmentMetadata();
}
}

Project setManualSpectrum(const Project& base, std::size_t index,
                          const std::string& particle,
                          const std::vector<double>& energy,
                          const std::vector<double>& values,
                          const std::string& interpolation) {
    base.validate();
    if (index > base.spectra.size()) throw std::runtime_error("Spectrum index outside project");
    for (std::size_t i = 0; i < base.spectra.size(); ++i)
        if (i != index && base.spectra[i].particle == particle)
            throw std::runtime_error("Another spectrum already uses this particle; edit its existing row");
    Spectrum spectrum;
    spectrum.particle = particle;
    spectrum.energy = energy;
    spectrum.values = values;
    spectrum.interpolation = interpolation;
    spectrum.provenance["manual_definition"] = {
        {"schema", "spacerad.manual-spectrum/1"},
        {"quantity", engine::quantityName(base.quantity)},
        {"angular_convention", "omnidirectional_scalar"}
    };
    spectrum.prepare();
    auto result = base;
    if (index == result.spectra.size()) result.spectra.push_back(spectrum);
    else result.spectra[index] = spectrum;
    markManual(result);
    result.validate();
    return result;
}

Project removeSpectrum(const Project& base, std::size_t index) {
    base.validate();
    if (index >= base.spectra.size()) throw std::runtime_error("Spectrum index outside project");
    if (base.spectra.size() == 1) throw std::runtime_error("At least one spectrum is required");
    auto result = base;
    result.spectra.erase(result.spectra.begin() + index);
    markManual(result);
    result.validate();
    return result;
}
}
