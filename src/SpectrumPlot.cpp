#include "SpectrumPlot.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace spacerad {
SpectrumPlot::SpectrumPlot(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(145);
    setAccessibleName(tr("微分谱曲线，线性坐标"));
}
void SpectrumPlot::setSpectra(const std::vector<Spectrum>& spectra, engine::SpectrumQuantity quantity) {
    spectra_ = spectra;
    quantity_ = quantity;
    update();
}
void SpectrumPlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    const QRectF area(75, 32, std::max(1, width() - 100), std::max(1, height() - 68));
    painter.setPen(QColor("#546c80"));
    painter.drawText(QRectF(8, 2, width() - 16, 24), Qt::AlignLeft,
                     tr("微分谱 · 线性坐标 · %1").arg(QString::fromStdString(engine::quantityUnit(quantity_))));
    if (spectra_.empty()) {
        painter.drawText(area, Qt::AlignCenter, tr("无有效谱"));
        return;
    }
    double low = spectra_.front().energy.front(), high = low, maximum = 0;
    for (const auto& s : spectra_) {
        low = std::min(low, s.energy.front());
        high = std::max(high, s.energy.back());
        for (double value : s.values) maximum = std::max(maximum, value);
    }
    if (!(high > low && maximum > 0)) return;
    const auto point = [&](double energy, double value) {
        return QPointF(area.left() + (energy - low) / (high - low) * area.width(),
                       area.bottom() - value / maximum * area.height());
    };
    painter.drawLine(area.bottomLeft(), area.bottomRight());
    painter.drawLine(area.bottomLeft(), area.topLeft());
    painter.drawText(QRectF(0, area.top() - 10, 68, 20), Qt::AlignRight, QString::number(maximum, 'g', 4));
    painter.drawText(QRectF(0, area.bottom() - 10, 68, 20), Qt::AlignRight, "0");
    painter.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 24), Qt::AlignLeft, QString::number(low, 'g', 4));
    painter.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 24), Qt::AlignRight,
                     QString::number(high, 'g', 4) + " MeV");
    const QColor colors[] = {QColor("#2166ac"), QColor("#b54d12"), QColor("#23845b"), QColor("#84479d"), QColor("#9a6415")};
    painter.save();
    painter.setClipRect(area.adjusted(-1, -1, 1, 1));
    int index = 0;
    for (const auto& s : spectra_) {
        painter.setPen(QPen(colors[index++ % 5], 2));
        QPainterPath path;
        path.moveTo(point(s.energy.front(), s.values.front()));
        if (s.interpolation == "bin_constant") {
            for (std::size_t i = 0; i < s.values.size(); ++i) {
                path.lineTo(point(s.energy[i + 1], s.values[i]));
                if (i + 1 < s.values.size()) path.lineTo(point(s.energy[i + 1], s.values[i + 1]));
            }
        } else {
            for (std::size_t i = 1; i < s.energy.size(); ++i) path.lineTo(point(s.energy[i], s.values[i]));
        }
        painter.drawPath(path);
    }
    painter.restore();
    int legend = int(area.left());
    index = 0;
    for (const auto& s : spectra_) {
        painter.setPen(colors[index++ % 5]);
        const auto label = QString::fromStdString(s.particle + " (" + s.interpolation + ")");
        painter.drawText(legend, int(area.top() + 14), label);
        legend += painter.fontMetrics().horizontalAdvance(label) + 20;
    }
}
}
