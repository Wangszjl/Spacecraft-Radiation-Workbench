#include <spacerad/WorkbenchWidget.h>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace spacerad;
namespace {
Json checks = Json::array();
void check(bool ok, const char* name) {
    checks.push_back({{"name", name}, {"passed", ok}});
    std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
    if (!ok) throw std::runtime_error(name);
}
QPushButton* button(QWidget& w, const char* name) { return w.findChild<QPushButton*>(name); }
void edit(WorkbenchWidget& widget, const char* action, const std::function<void(QDialog&)>& operate) {
    bool called = false;
    QString failure;
    QTimer::singleShot(0, &widget, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { failure = "No active spectrum dialog"; return; }
        try {
            if (dialog->objectName() != "spaceradSpectrumDialog") throw std::runtime_error("Wrong modal dialog");
            called = true;
            operate(*dialog);
        } catch (const std::exception& e) { failure = QString::fromUtf8(e.what()); dialog->reject(); }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &widget, [&] {
        failure = "Spectrum modal timed out";
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    watchdog.start(10000);
    button(widget, action)->click();
    watchdog.stop();
    if (!called || !failure.isEmpty()) throw std::runtime_error(failure.isEmpty() ? "Modal not opened" : failure.toStdString());
}
void points(QDialog& dialog, const char* particle, const std::vector<double>& energy,
            const std::vector<double>& values, const char* mode = "linear") {
    auto* species = dialog.findChild<QComboBox*>("spaceradSpectrumParticle");
    auto* interpolation = dialog.findChild<QComboBox*>("spaceradSpectrumInterpolation");
    auto* table = dialog.findChild<QTableWidget*>("spaceradSpectrumPoints");
    if (!species || !interpolation || !table) throw std::runtime_error("Missing spectrum editor controls");
    species->setCurrentIndex(species->findData(particle));
    interpolation->setCurrentIndex(interpolation->findData(mode));
    table->setRowCount(int(energy.size()));
    for (int row = 0; row < table->rowCount(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(QString::number(energy[row], 'g', 17)));
        table->setItem(row, 1, new QTableWidgetItem(row < int(values.size()) ? QString::number(values[row], 'g', 17) : QString{}));
    }
}
QPushButton* answer(QDialog& dialog, QDialogButtonBox::StandardButton which) {
    return dialog.findChild<QDialogButtonBox*>("spaceradSpectrumButtons")->button(which);
}
Json run(WorkbenchWidget& widget, const QString& directory) {
    QEventLoop loop;
    QByteArray accepted;
    QString error;
    auto done = QObject::connect(&widget, &WorkbenchWidget::resultReady, &loop, [&](const QByteArray& bytes) { accepted = bytes; loop.quit(); });
    auto failed = QObject::connect(widget.service(), &AnalysisService::failed, &loop, [&](const QString& message) { error = message; loop.quit(); });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] { error = "Transport timed out"; widget.cancel(); loop.quit(); });
    watchdog.start(60000);
    widget.runTo(directory);
    check(!button(widget, "spaceradAddSpectrum")->isEnabled() && !button(widget, "spaceradEditSpectrum")->isEnabled() &&
          !button(widget, "spaceradConvertQuantity")->isEnabled(), "Environment controls disabled during transport");
    loop.exec();
    QObject::disconnect(done); QObject::disconnect(failed);
    if (accepted.isEmpty()) throw std::runtime_error(error.isEmpty() ? "No accepted result" : error.toStdString());
    return Json::parse(accepted.toStdString());
}
}
int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    if (argc != 3) return 2;
    const QString output = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    if (QFileInfo::exists(output) || !QDir().mkpath(output)) return 2;
    bool passed = false;
    try {
        WorkbenchWidget widget;
        widget.resize(1320, 1100);
        widget.setRuntime(loadRuntimeFile(QString::fromLocal8Bit(argv[1])));
        auto base = Project::demo(); base.events = 20000; base.duration = 64;
        base.environmentMetadata["model"] = "Declared orbit model fixture";
        widget.setProject(base); widget.show();
        widget.findChild<QTabWidget*>()->setCurrentIndex(1);
        QApplication::processEvents();
        widget.resize(950, 720);
        QApplication::processEvents();
        auto* scroll = widget.findChild<QScrollArea*>("spaceradScrollArea");
        check(scroll && widget.height() == 720 && scroll->verticalScrollBar()->maximum() > 0,
              "Small desktop or embedding window retains scroll access to all controls");
        widget.resize(1320, 1200);
        QApplication::processEvents();
        auto* sources = widget.findChild<QTableWidget*>("spaceradSpectra");
        auto* normalization = widget.findChild<QLabel*>("spaceradNormalization");
        check(sources && normalization && widget.findChild<QWidget*>("spaceradEnvironmentPlot"), "Environment table, preview and normalization are exposed");
        sources->setCurrentCell(0, 0);
        const auto beforeCancel = widget.project().toJson();
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            points(dialog, "proton", {1, 2}, {8, 1}); answer(dialog, QDialogButtonBox::Cancel)->click();
        });
        check(widget.project().toJson() == beforeCancel, "Cancelling an edit preserves exact project");
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            points(dialog, "proton", {3, 2, 1}, {1, 1, 1}); answer(dialog, QDialogButtonBox::Ok)->click();
            check(dialog.isVisible() && !dialog.findChild<QLabel*>("spaceradSpectrumError")->text().isEmpty(), "Unsorted energy rejected inline without closing dialog");
            answer(dialog, QDialogButtonBox::Cancel)->click();
        });
        check(widget.project().toJson() == beforeCancel, "Rejected edit never partially commits");
        auto newer = base; newer.name = "Host update during spectrum dialog";
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            points(dialog, "proton", {10, 20, 30}, {1, 1, 1}); widget.setProject(newer);
            answer(dialog, QDialogButtonBox::Ok)->click();
        });
        check(widget.project().toJson() == newer.toJson(), "Stale spectrum modal cannot overwrite host project update");
        sources->setCurrentCell(0, 0);
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            points(dialog, "proton", {10, 20, 30}, {1, 1, 1});
            check(dialog.findChild<QLabel*>("spaceradSpectrumIntegral")->text().contains("20"), "Live editor preview reports analytic linear integral");
            check(dialog.grab().save(output + "/spectrum-editor.png"), "Spectrum editor screenshot saved");
            answer(dialog, QDialogButtonBox::Ok)->click();
        });
        check(widget.project().spectra[0].integral == 20 && widget.project().synthetic &&
              widget.project().environmentMetadata == engine::defaultEnvironmentMetadata(), "Manual spectrum is prepared and clears stale orbit metadata");
        check(widget.project().toJson()["volumes"] == base.toJson()["volumes"], "Spectrum edit preserves all authored geometry");
        edit(widget, "spaceradAddSpectrum", [&](QDialog& dialog) {
            points(dialog, "e-", {1, 2, 3}, {4, 2, 0}); answer(dialog, QDialogButtonBox::Ok)->click();
        });
        check(widget.project().spectra.size() == 2 && widget.project().integratedSpectrum() == 24, "Electron spectrum appended to proton environment");
        check(std::abs(sources->item(1, 5)->text().toDouble() - 100.0 / 6) < .0001, "Displayed particle sampling fraction matches integrals");
        const auto combined = widget.project();
        edit(widget, "spaceradAddSpectrum", [&](QDialog& dialog) {
            points(dialog, "proton", {1, 2}, {1, 1}); answer(dialog, QDialogButtonBox::Ok)->click();
            check(dialog.isVisible() && !dialog.findChild<QLabel*>("spaceradSpectrumError")->text().isEmpty(), "Duplicate particle requires editing existing spectrum");
            answer(dialog, QDialogButtonBox::Cancel)->click();
        });
        check(widget.project().toJson() == combined.toJson(), "Duplicate spectrum rejection preserves original mixture");
        sources->setCurrentCell(1, 0);
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            points(dialog, "e-", {1, 2, 3}, {4, 2}, "bin_constant"); answer(dialog, QDialogButtonBox::Ok)->click();
        });
        check(widget.project().spectra[1].interpolation == "bin_constant" && widget.project().spectra[1].integral == 6, "Bin-constant editor preserves edge and value counts");
        sources->setCurrentCell(1, 0);
        edit(widget, "spaceradEditSpectrum", [&](QDialog& dialog) {
            auto* table = dialog.findChild<QTableWidget*>("spaceradSpectrumPoints");
            check(table->rowCount() == 3 && table->item(2, 1)->text().isEmpty(), "Reopening bin spectrum leaves last edge value empty");
            points(dialog, "e-", {1, 2, 3}, {4, 2, 0}); answer(dialog, QDialogButtonBox::Ok)->click();
        });
        check(widget.project().spectra[1].integral == 4, "Explicit linear definition restores intended electron integral");
        sources->setCurrentCell(1, 0); button(widget, "spaceradRemoveSpectrum")->click();
        check(widget.project().spectra.size() == 1 && widget.project().spectra[0].particle == "proton", "Selected spectrum can be removed");
        sources->setCurrentCell(0, 0); const auto sole = widget.project().toJson();
        button(widget, "spaceradRemoveSpectrum")->click();
        check(widget.project().toJson() == sole, "Last spectrum removal rejected without losing environment");
        widget.setProject(combined);
        const auto analytic = engine::pi * 50 * 50 * 24 * 64;
        check(std::abs(widget.project().expectedPrimaries() - analytic) < 1e-7 && normalization->text().contains("24"), "Displayed normalization follows pi R squared integral times duration");
        widget.saveProject(output + "/manual-project.json"); widget.loadProject(output + "/manual-project.json");
        check(widget.project().toJson()["environment"] == combined.toJson()["environment"], "Saved environment reload preserves points, quantity and manual provenance");
        const auto flux = run(widget, output + "/flux-run");
        check(flux["histories"] == 20000 && flux["targets"].size() == 3 && flux["project"]["environment"]["spectra"].size() == 2, "Two-particle flux transport completed with three scored targets");
        button(widget, "spaceradConvertQuantity")->click();
        auto fluence = widget.project();
        check(widget.acceptedResult().isEmpty() && fluence.quantity == engine::SpectrumQuantity::Fluence &&
              fluence.spectra[1].values[0] == 256 && fluence.expectedPrimaries() == analytic, "Flux to fluence scales values, preserves normalization and clears old result");
        check(fluence.spectra[1].provenance["conversions"].back()["duration_s"] == 64, "Quantity conversion records duration and factor provenance");
        auto* duration = widget.findChild<QLineEdit*>("spaceradDuration"); duration->setText("128");
        check(widget.project().expectedPrimaries() == analytic && normalization->text().contains(QString::fromUtf8("不再乘时长")), "Fluence normalization is independent of later exposure changes");
        duration->setText("64");
        widget.saveProject(output + "/fluence-project.json");
        const auto result = run(widget, output + "/fluence-run");
        check(result["targets"] == flux["targets"] && result["score_moments"] == flux["score_moments"], "Equivalent flux and fluence produce identical seeded doses and moments");
        check(result["expected_primaries"] == flux["expected_primaries"] &&
              result["normalization"].get<std::string>().find("duration_s is context only") != std::string::npos &&
              flux["normalization"].get<std::string>().find("omni_flux*duration_s") != std::string::npos,
              "Equivalent runs retain normalization while reporting their different quantity conventions");
        QApplication::processEvents();
        check(widget.grab().save(output + "/environment.png"), "Environment workflow screenshot saved");
        button(widget, "spaceradConvertQuantity")->click();
        check(widget.project().quantity == engine::SpectrumQuantity::Flux && widget.project().spectra[1].values == combined.spectra[1].values &&
              widget.project().spectra[1].provenance["conversions"].size() == 2, "Reverse conversion restores values and retains both conversion records");
        passed = true;
    } catch (const std::exception& e) { checks.push_back({{"name", e.what()}, {"passed", false}}); std::cerr << e.what() << std::endl; }
    QFile file(output + "/summary.json");
    if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(Json({{"schema", "spacerad.widget-environment-test/1"}, {"passed", passed}, {"checks", checks}}).dump(2).c_str());
    return passed ? 0 : 1;
}
