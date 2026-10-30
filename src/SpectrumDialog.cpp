#include "SpectrumDialog.h"
#include "SpectrumPlot.h"
#include "spacerad/Environment.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace spacerad {
std::optional<Project> editSpectrumDialog(QWidget* parent, const Project& source, std::size_t index) {
    if (index > source.spectra.size()) throw std::runtime_error("Spectrum index outside project");
    Spectrum initial;
    if (index < source.spectra.size()) initial = source.spectra[index];
    else {
        bool hasElectron = false;
        for (const auto& s : source.spectra) hasElectron |= s.particle == "e-";
        initial.particle = hasElectron ? "proton" : "e-";
        if (initial.particle == "e-") { initial.energy = {.1, .5, 1, 3, 10}; initial.values = {100, 60, 20, 2, .1}; }
        else { initial.energy = {10, 30, 100, 300}; initial.values = {100, 35, 8, 1}; }
        initial.prepare();
    }
    if (initial.energy.size() > 2048) throw std::runtime_error("Manual editor supports up to 2048 energy rows; existing input is unchanged");
    std::unique_ptr<QDialog> owner(new QDialog(parent));
    QPointer<QDialog> alive(owner.get());
    auto& dialog = *owner;
    dialog.setObjectName("spaceradSpectrumDialog");
    dialog.setWindowTitle(index == source.spectra.size() ? QObject::tr("添加简单微分谱") : QObject::tr("编辑微分谱"));
    dialog.resize(700, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* convention = new QLabel(QObject::tr("人工设置 → 合成环境；不执行轨道模型。各向同性、全向标量微分谱，能量为每粒子 MeV。单位：%1。修改时会清除全局轨道模型元数据，保留其他谱的来源记录。")
        .arg(QString::fromStdString(engine::quantityUnit(source.quantity))), &dialog);
    convention->setWordWrap(true);
    layout->addWidget(convention);
    auto* options = new QHBoxLayout;
    auto* particle = new QComboBox(&dialog);
    particle->setObjectName("spaceradSpectrumParticle");
    for (auto name : {"e-", "proton", "alpha", "gamma", "neutron"}) particle->addItem(name, name);
    particle->setCurrentIndex(particle->findData(QString::fromStdString(initial.particle)));
    options->addWidget(particle);
    auto* interpolation = new QComboBox(&dialog);
    interpolation->setObjectName("spaceradSpectrumInterpolation");
    interpolation->addItem(QObject::tr("点间线性微分值"), "linear");
    interpolation->addItem(QObject::tr("分箱常数微分值"), "bin_constant");
    interpolation->setCurrentIndex(interpolation->findData(QString::fromStdString(initial.interpolation)));
    options->addWidget(interpolation);
    auto* add = new QPushButton(QObject::tr("添加能量行"), &dialog);
    add->setObjectName("spaceradSpectrumAddRow");
    auto* remove = new QPushButton(QObject::tr("删除选中行"), &dialog);
    remove->setObjectName("spaceradSpectrumRemoveRow");
    options->addWidget(add); options->addWidget(remove); layout->addLayout(options);
    auto* table = new QTableWidget(int(initial.energy.size()), 2, &dialog);
    table->setObjectName("spaceradSpectrumPoints");
    table->setHorizontalHeaderLabels({"energy_MeV", QString::fromStdString(engine::quantityUnit(source.quantity))});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int row = 0; row < table->rowCount(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(QString::number(initial.energy[row], 'g', 17)));
        table->setItem(row, 1, new QTableWidgetItem(row < int(initial.values.size()) ? QString::number(initial.values[row], 'g', 17) : QString{}));
    }
    layout->addWidget(table, 1);
    auto* hint = new QLabel(QObject::tr("能量必须为正且严格递增；微分值有限且非负，积分须大于0。分箱模式最后一行仅填写右边界能量，微分值必须留空；不会自动排序或将积分谱当成微分谱。"), &dialog);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* plot = new SpectrumPlot(&dialog); plot->setObjectName("spaceradSpectrumPreview"); layout->addWidget(plot);
    auto* integral = new QLabel(&dialog); integral->setObjectName("spaceradSpectrumIntegral"); layout->addWidget(integral);
    auto* error = new QLabel(&dialog); error->setObjectName("spaceradSpectrumError"); error->setWordWrap(true); error->setStyleSheet("color:#a73520;"); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("spaceradSpectrumButtons"); layout->addWidget(buttons);
    auto readSpectrum = [&] {
        Spectrum spectrum;
        spectrum.particle = particle->currentData().toString().toStdString();
        spectrum.interpolation = interpolation->currentData().toString().toStdString();
        const bool bins = spectrum.interpolation == "bin_constant";
        auto number = [&](int row, int column) {
            bool ok = false;
            auto* item = table->item(row, column);
            const double value = item ? item->text().toDouble(&ok) : 0;
            if (!ok || !std::isfinite(value)) throw std::runtime_error("Enter finite energy and differential values in every required cell");
            return value;
        };
        for (int row = 0; row < table->rowCount(); ++row) {
            spectrum.energy.push_back(number(row, 0));
            if (!bins || row + 1 < table->rowCount()) spectrum.values.push_back(number(row, 1));
            else if (table->item(row, 1) && !table->item(row, 1)->text().trimmed().isEmpty())
                throw std::runtime_error("Last bin-edge row must have an empty differential value");
        }
        spectrum.prepare();
        return spectrum;
    };
    auto preview = [&] {
        try {
            auto spectrum = readSpectrum();
            plot->setSpectra({spectrum}, source.quantity);
            integral->setText(QObject::tr("能量积分：%1 %2").arg(QString::number(spectrum.integral, 'g', 12),
                source.quantity == engine::SpectrumQuantity::Flux ? "cm-2 s-1" : "cm-2"));
        } catch (const std::exception& e) {
            plot->setSpectra({}, source.quantity);
            integral->setText(QObject::tr("预览未就绪：%1").arg(QString::fromUtf8(e.what())));
        }
    };
    QObject::connect(table, &QTableWidget::cellChanged, &dialog, [&](int, int) { preview(); });
    QObject::connect(particle, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { preview(); });
    QObject::connect(interpolation, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { preview(); });
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        if (table->rowCount() >= 2048) { error->setText(QObject::tr("最多2048个能量行。")); return; }
        table->insertRow(table->rowCount()); preview();
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (table->rowCount() <= 2 || table->currentRow() < 0) return;
        table->removeRow(table->currentRow()); preview();
    });
    std::optional<Project> result;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            auto spectrum = readSpectrum();
            result = setManualSpectrum(source, index, spectrum.particle, spectrum.energy, spectrum.values, spectrum.interpolation);
            dialog.accept();
        } catch (const std::exception& e) { error->setText(QString::fromUtf8(e.what())); }
    });
    preview();
    const auto code = dialog.exec();
    if (!alive) { owner.release(); return std::nullopt; }
    if (code != QDialog::Accepted) return std::nullopt;
    return result;
}
}
