#pragma once
#include "spacerad/Project.h"
#include <QWidget>

namespace spacerad {
// Linear axes display the exact piecewise-linear / bin-constant definition.
class SpectrumPlot final : public QWidget {
public:
    explicit SpectrumPlot(QWidget* parent = nullptr);
    void setSpectra(const std::vector<Spectrum>& spectra, engine::SpectrumQuantity quantity);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<Spectrum> spectra_;
    engine::SpectrumQuantity quantity_ = engine::SpectrumQuantity::Flux;
};
}
