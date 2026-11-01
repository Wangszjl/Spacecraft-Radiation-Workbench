#include <spacerad/WorkbenchWidget.h>
#include <spacerad/Results.h>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>
#include <stdexcept>

using namespace spacerad;
namespace {
Json checks = Json::array();
void check(bool ok, const char* name) {
    checks.push_back({{"name", name}, {"passed", ok}});
    if (!ok) throw std::runtime_error(name);
}
void exportFile(QDialog& viewer, const char* action, const QString& path) {
    bool reached = false;
    QTimer::singleShot(50, &viewer, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            reached = true; dialog->setDirectory(QFileInfo(path).absolutePath()); dialog->selectFile(QFileInfo(path).fileName());
            if (auto* filename = dialog->findChild<QLineEdit*>("fileNameEdit")) filename->setText(QFileInfo(path).fileName());
            QTimer::singleShot(100, dialog, [dialog] { QMetaObject::invokeMethod(dialog, "accept"); });
        }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &viewer, [&] { if (auto* modal = QApplication::activeModalWidget()) modal->close(); });
    watchdog.start(5000);
    viewer.findChild<QPushButton*>(action)->click();
    if (!QFile::exists(path)) std::cerr << "Export failure: reached=" << reached << ", status=" << viewer.findChild<QLabel*>("spaceradComparisonStatus")->text().toStdString() << '\n';
    check(reached && QFile::exists(path), "Native viewer exports through file picker");
}
}
int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (argc != 4) return 2;
    const auto a = QString::fromLocal8Bit(argv[1]), b = QString::fromLocal8Bit(argv[2]), output = QString::fromLocal8Bit(argv[3]);
    if (QDir(output).exists() || !QDir().mkpath(output)) return 2;
    bool passed = false;
    try {
        WorkbenchWidget widget; widget.resize(1100, 850); widget.show(); app.processEvents();
        const auto project = widget.project().toJson(); const auto result = widget.acceptedResult();
        int changed = 0, ready = 0;
        QObject::connect(&widget, &WorkbenchWidget::projectChanged, &widget, [&] { ++changed; });
        QObject::connect(&widget, &WorkbenchWidget::resultReady, &widget, [&] { ++ready; });
        widget.showResults(a, b); app.processEvents();
        auto* viewer = widget.findChild<QDialog*>("spaceradResultsDialog");
        check(viewer && viewer->isVisible() && !viewer->isModal(), "Shared widget opens an independent nonmodal results viewer");
        auto* table = viewer->findChild<QTableWidget*>("spaceradComparisonTable");
        auto* csv = viewer->findChild<QPushButton*>("spaceradComparisonCsv");
        auto* json = viewer->findChild<QPushButton*>("spaceradComparisonJson");
        auto* status = viewer->findChild<QLabel*>("spaceradComparisonStatus");
        const auto comparison = compareRuns(loadArchivedRun(a), loadArchivedRun(b));
        check(table->rowCount() == int(comparison.at("rows").size()) && csv->isEnabled() && json->isEnabled(), "Real archived jobs populate comparison and export controls");
        for (int i = 0; i < table->rowCount(); ++i) {
            const auto& row = comparison.at("rows").at(i);
            check(table->item(i, 0)->text().toStdString() == row.at("id") && table->item(i, 7)->text().toDouble() == row.at("delta_Gy").get<double>(), "Displayed target ID and dose difference retain precision");
        }
        check(viewer->findChild<QLabel*>("spaceradArchiveIdentity")->text().contains(QString::fromStdString(comparison.at("baseline").at("result_sha256"))), "Viewer exposes saved result fingerprint");
        check(status->text().contains(QString::fromUtf8("协方差")), "Viewer explains unknown difference uncertainty");
        exportFile(*viewer, "spaceradComparisonCsv", output + "/comparison.csv");
        exportFile(*viewer, "spaceradComparisonJson", output + "/comparison.json");
        QFile exported(output + "/comparison.json"); exported.open(QIODevice::ReadOnly);
        check(Json::parse(exported.readAll().toStdString()) == comparison, "Exported JSON equals loaded comparison including provenance");
        QFile csvFile(output + "/comparison.csv"); csvFile.open(QIODevice::ReadOnly);
        check(csvFile.readAll() == comparisonCsv(comparison), "Exported CSV equals precise numeric table");
        check(status->text().contains(QString::fromUtf8("协方差")), "Export feedback preserves comparison uncertainty notice");
        viewer->grab().save(output + "/comparison.png");
        viewer->findChild<QPushButton*>("spaceradHistoryClearB")->click();
        check(!csv->isEnabled() && json->isEnabled() && table->rowCount() > 0, "Clearing B keeps A for offline review");
        auto* bpath = viewer->findChild<QLineEdit*>("spaceradArchiveB");
        bpath->setText(output + "/missing"); viewer->findChild<QPushButton*>("spaceradArchiveLoadB")->click();
        check(!csv->isEnabled() && status->text().contains(QString::fromUtf8("失败")), "Invalid archive is reported without stale comparison export");
        auto* apath = viewer->findChild<QLineEdit*>("spaceradArchiveA");
        apath->setText(output + "/missing-A"); viewer->findChild<QPushButton*>("spaceradArchiveLoadA")->click();
        check(!json->isEnabled() && table->rowCount() == 0, "Invalid baseline clears displayed and exportable result");
        auto* jobs = viewer->findChild<QTreeWidget*>("spaceradHistoryJobs");
        check(jobs->topLevelItemCount() > 0, "History list discovers saved job directories");
        for (int i=0; i<jobs->topLevelItemCount(); ++i) if (QFileInfo(jobs->topLevelItem(i)->data(0,Qt::UserRole).toString()).absoluteFilePath()==QFileInfo(a).absoluteFilePath()) jobs->setCurrentItem(jobs->topLevelItem(i));
        viewer->findChild<QPushButton*>("spaceradHistoryUseA")->click();
        check(json->isEnabled(), "Selecting historical job reloads baseline");
        check(widget.project().toJson() == project && widget.acceptedResult() == result && changed == 0 && ready == 0 && !widget.service()->running(), "Archive viewing preserves host project, current result and service signals");
        viewer->grab().save(output + "/results.png"); widget.grab().save(output + "/workbench.png");
        viewer->close(); app.processEvents();
        widget.findChild<QPushButton*>("spaceradHistory")->click(); app.processEvents();
        check(widget.findChild<QDialog*>("spaceradResultsDialog"), "History toolbar opens viewer without a completed current run");
        passed = true;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    QFile file(output + "/summary.json"); file.open(QIODevice::WriteOnly);
    file.write(QByteArray::fromStdString(Json{{"passed", passed}, {"checks", checks}}.dump(2)));
    std::cout << checks.size() << " native results checks; passed=" << passed << '\n';
    return passed ? 0 : 1;
}
