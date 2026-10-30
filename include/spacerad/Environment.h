#pragma once
#include "Project.h"
#include <cstddef>

namespace spacerad {
// index == spectra.size() appends; an existing index replaces that spectrum.
// Values are differential omnidirectional scalar flux/fluence in base.quantity.
// Manual changes mark the environment synthetic and clear orbit-model metadata.
Project setManualSpectrum(const Project& base, std::size_t index,
                          const std::string& particle,
                          const std::vector<double>& energy,
                          const std::vector<double>& values,
                          const std::string& interpolation = "linear");
Project removeSpectrum(const Project& base, std::size_t index);
}
