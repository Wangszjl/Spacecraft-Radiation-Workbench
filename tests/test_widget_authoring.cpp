#include <spacerad/WorkbenchWidget.h>
#include <spacerad/Project.h>
#include <spacerad/Authoring.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>

#include <filesystem>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using spacerad::Json;
using spacerad::Project;
using spacerad::WorkbenchWidget;

struct Report {
    Json checks = Json::array();
    Json failures = Json::array();

    void check(bool ok, const QString& name, const QString& detail = {}) {
        Json item = {{"name", name.toStdString()}, {"passed", ok}};
        if (!detail.isEmpty()) item["detail"] = detail.toStdString();
        checks.push_back(item);
        if (!ok) failures.push_back(item);
        std::cout << (ok ? "PASS " : "FAIL ") << name.toStdString();
        if (!detail.isEmpty()) std::cout << ": " << detail.toStdString();
        std::cout << std::endl;
    }

    void save(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        Json result = {{"schema", "spacerad.widget-authoring-test/1"},
                       {"checks", checks}, {"failures", failures},
                       {"passed", failures.empty()}};
        const auto bytes = result.dump(2);
        file.write(bytes.data(), static_cast<qint64>(bytes.size()));
    }
};

QPushButton* button(QWidget* parent, const char* name) {
    return parent->findChild<QPushButton*>(QString::fromLatin1(name));
}

int volumeRow(QTableWidget* table, const QString& id) {
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0) && table->item(row, 0)->text() == id) return row;
    return -1;
}

bool runPartAction(WorkbenchWidget& widget, const char* action, const QString& id,
                   bool duplicateThenCancel = false) {
    auto* actionButton = button(&widget, action);
    if (!actionButton) return false;
    QString callbackError;
    bool dialogFound = false;
    QTimer::singleShot(0, &widget, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("spaceradPartDialog")) {
            callbackError = QStringLiteral("Expected active spaceradPartDialog");
            return;
        }
        dialogFound = true;
        auto* idEdit = dialog->findChild<QLineEdit*>(QStringLiteral("spaceradPartId"));
        auto* shape = dialog->findChild<QComboBox*>(QStringLiteral("spaceradPartShape"));
        auto* material = dialog->findChild<QComboBox*>(QStringLiteral("spaceradPartMaterial"));
        auto* sensitive = dialog->findChild<QCheckBox*>(QStringLiteral("spaceradPartSensitive"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("spaceradPartButtons"));
        bool coordinateFieldsPresent = true;
        for (int axis = 0; axis < 3; ++axis) {
            coordinateFieldsPresent = coordinateFieldsPresent &&
                dialog->findChild<QLineEdit*>(QStringLiteral("spaceradPartCenter%1").arg(axis)) &&
                dialog->findChild<QLineEdit*>(QStringLiteral("spaceradPartSize%1").arg(axis));
        }
        if (!idEdit || !shape || !material || !sensitive || !buttons || !buttons->button(QDialogButtonBox::Ok) ||
            !buttons->button(QDialogButtonBox::Cancel) || !coordinateFieldsPresent) {
            callbackError = QStringLiteral("Part dialog is missing an expected editor");
            if (dialog) dialog->reject();
            return;
        }
        {
            QSignalBlocker blocker(idEdit);
            idEdit->setText(id);
        }
        if (shape->currentData().toString() != QStringLiteral("box"))
            shape->setCurrentIndex(shape->findData(QStringLiteral("box")));
        if(QString::fromLatin1(action)==QStringLiteral("spaceradAddPart")){material->setCurrentText(QStringLiteral("G4_Si"));sensitive->setChecked(true);}
        buttons->button(QDialogButtonBox::Ok)->click();
        if (duplicateThenCancel) {
            auto* error = dialog->findChild<QLabel*>(QStringLiteral("spaceradPartError"));
            if (!dialog->isVisible() || !error || error->text().isEmpty())
                callbackError = QStringLiteral("Duplicate ID did not leave an inline validation error");
            buttons->button(QDialogButtonBox::Cancel)->click();
        }
    });
    QTimer watchdog;watchdog.setSingleShot(true);
    QObject::connect(&watchdog,&QTimer::timeout,&widget, [] {
        if (auto* modal = QApplication::activeModalWidget())
            if (modal->objectName() == QStringLiteral("spaceradPartDialog"))
                qobject_cast<QDialog*>(modal)->reject();
    });
    watchdog.start(5000);actionButton->click();watchdog.stop();
    return dialogFound && callbackError.isEmpty();
}

bool acceptAddAfterHostProjectUpdate(WorkbenchWidget& widget, const Project& hostProject,
                                     const QString& id) {
    auto* addButton = button(&widget, "spaceradAddPart");
    if (!addButton) return false;
    QString callbackError;
    bool dialogFound = false;
    QTimer::singleShot(0, &widget, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("spaceradPartDialog")) {
            callbackError = QStringLiteral("Expected active spaceradPartDialog for stale-add check");
            return;
        }
        dialogFound = true;
        auto* idEdit = dialog->findChild<QLineEdit*>(QStringLiteral("spaceradPartId"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("spaceradPartButtons"));
        if (!idEdit || !buttons || !buttons->button(QDialogButtonBox::Ok)) {
            callbackError = QStringLiteral("Stale-add dialog is missing ID or Ok control");
            dialog->reject();
            return;
        }
        widget.setProject(hostProject);
        {
            QSignalBlocker blocker(idEdit);
            idEdit->setText(id);
        }
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    QTimer watchdog;watchdog.setSingleShot(true);
    QObject::connect(&watchdog,&QTimer::timeout,&widget, [] {
        if (auto* modal = QApplication::activeModalWidget())
            if (modal->objectName() == QStringLiteral("spaceradPartDialog"))
                qobject_cast<QDialog*>(modal)->reject();
    });
    watchdog.start(5000);addButton->click();watchdog.stop();
    return dialogFound && callbackError.isEmpty();
}

bool addCurvedPart(WorkbenchWidget& widget,const std::string& kind) {
    const auto proposal=spacerad::suggestPart(widget.project(),kind,true);
    bool accepted=false;QTimer watchdog;watchdog.setSingleShot(true);
    QObject::connect(&watchdog,&QTimer::timeout,&widget,[]{if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();});
    QTimer::singleShot(0,&widget,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        auto* id=dialog->findChild<QLineEdit*>("spaceradPartId");auto* shape=dialog->findChild<QComboBox*>("spaceradPartShape");auto* buttons=dialog->findChild<QDialogButtonBox*>("spaceradPartButtons");
        if(!id||!shape||!buttons){dialog->reject();return;}
        id->setText(QString::fromStdString("Curve_"+kind));shape->setCurrentIndex(shape->findData(QString::fromStdString(kind)));
        for(int k=0;k<3;++k){dialog->findChild<QLineEdit*>(QString("spaceradPartCenter%1").arg(k))->setText(QString::number(proposal.center[k],'g',17));dialog->findChild<QLineEdit*>(QString("spaceradPartSize%1").arg(k))->setText(QString::number(proposal.size[k],'g',17));}
        buttons->button(QDialogButtonBox::Ok)->click();accepted=!dialog->isVisible();if(!accepted)dialog->reject();
    });
    watchdog.start(5000);button(&widget,"spaceradAddPart")->click();watchdog.stop();return accepted;
}

void saveSummary(Report& report, const QString& output, const QString& error = {}) {
    if (!error.isEmpty()) report.check(false, QStringLiteral("test execution"), error);
    report.save(output + QStringLiteral("/summary.json"));
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    Report report;
    QString output;
    if (argc < 3) {
        std::cerr << "Usage: test_widget_authoring <runtimeJSON> <NEWOUTPUT>" << std::endl;
        return 2;
    }
    output = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    if(QFileInfo::exists(output)||!QDir().mkpath(output))return 2;
    try {
        const auto runtime = spacerad::loadRuntimeFile(QString::fromLocal8Bit(argv[1]));
        WorkbenchWidget widget;
        widget.setRuntime(runtime);
        widget.setProject(Project::assemblyDemo());
        widget.resize(1450, 1050);
        widget.show();
        QApplication::processEvents();

        auto* volumes = widget.findChild<QTableWidget*>(QStringLiteral("spaceradVolumes"));
        auto* results = widget.findChild<QTableWidget*>(QStringLiteral("spaceradResults"));
        report.check(volumes && results, QStringLiteral("new Workbench exposes expected tables"));
        if (!volumes || !results) throw std::runtime_error("Required workbench tables are absent");

        const auto original = widget.project();
        const Json originalJson = original.toJson();
        const auto originalVolumes = originalJson.at("volumes");
        if (auto* fresh = button(&widget, "spaceradNewProject")) fresh->click();
        report.check(widget.project().volumes.size() == Project::demo().volumes.size(),
                     QStringLiteral("New Project loads the basic spacecraft project"));
        widget.setProject(Project::assemblyDemo());

        Project hostUpdate = widget.project();
        hostUpdate.name = "Host update during dialog";
        const bool staleAddDialogAccepted = acceptAddAfterHostProjectUpdate(
            widget, hostUpdate, QStringLiteral("Stale_Dialog_Add"));
        report.check(staleAddDialogAccepted && widget.project().name == hostUpdate.name &&
                         widget.project().toJson().at("volumes") == originalVolumes,
                     QStringLiteral("Host project update during Add dialog is not overwritten by stale editor"));
        widget.setProject(original);

        for(const auto& shape:std::array<std::string,2>{"cylinder","sphere"}){
            auto before=widget.project().toJson().at("volumes");bool accepted=addCurvedPart(widget,shape);auto project=widget.project();
            report.check(accepted&&project.volumes.back().shape==shape&&project.volumes.back().size[0]==project.volumes.back().size[1]&&(shape!="sphere"||project.volumes.back().size[0]==project.volumes.back().size[2]),QString::fromStdString("Dialog creates valid "+shape+" dimensions"));
            widget.selectComponent(QString::fromStdString("Curve_"+shape));button(&widget,"spaceradRemovePart")->click();report.check(widget.project().toJson().at("volumes")==before,QString::fromStdString("Removing "+shape+" restores source geometry"));
        }
        bool addClicked = runPartAction(widget, "spaceradAddPart", QStringLiteral("Added_Si"));
        auto afterAdd = widget.project();
        report.check(addClicked && afterAdd.volumes.size() == original.volumes.size() + 1,
                     QStringLiteral("Add creates a part using the modal editor"));
        int addedIndex = -1;
        for (int i = 0; i < int(afterAdd.volumes.size()); ++i)
            if (afterAdd.volumes[size_t(i)].id == "Added_Si") addedIndex = i;
        bool originalsPreservedAfterAdd = afterAdd.volumes.size() >= original.volumes.size();
        for (size_t i = 0; originalsPreservedAfterAdd && i < original.volumes.size(); ++i)
            originalsPreservedAfterAdd = afterAdd.toJson().at("volumes").at(i) == originalVolumes.at(i);
        report.check(addedIndex >= 0 && afterAdd.volumes[size_t(addedIndex)].sensitive &&
                         afterAdd.volumes[size_t(addedIndex)].material == "G4_Si" && originalsPreservedAfterAdd,
                     QStringLiteral("Add defaults to sensitive silicon and preserves source geometry"));

        auto* editButton = button(&widget, "spaceradEditPart");
        int row = volumeRow(volumes, QStringLiteral("Added_Si"));
        if (row >= 0) volumes->setCurrentCell(row, 0);
        bool editClicked = editButton && runPartAction(widget, "spaceradEditPart", QStringLiteral("Edited_Si"));
        auto edited = widget.project();
        bool originalsPreservedAfterEdit = edited.volumes.size() >= original.volumes.size();
        for (size_t i = 0; originalsPreservedAfterEdit && i < original.volumes.size(); ++i)
            originalsPreservedAfterEdit = edited.toJson().at("volumes").at(i) == originalVolumes.at(i);
        report.check(editClicked && volumeRow(volumes, QStringLiteral("Edited_Si")) >= 0 &&
                         edited.volumes.size() == original.volumes.size() + 1 && originalsPreservedAfterEdit,
                     QStringLiteral("Edit renames the selected part without changing count"));
        row = volumeRow(volumes, QStringLiteral("Edited_Si"));
        if (row >= 0) volumes->setCurrentCell(row, 0);
        bool cancelledEdit = false;
        QTimer::singleShot(0, &widget, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == QStringLiteral("spaceradPartDialog")) {
                auto* idEdit = dialog->findChild<QLineEdit*>(QStringLiteral("spaceradPartId"));
                if (idEdit) { QSignalBlocker blocker(idEdit); idEdit->setText(QStringLiteral("Should_Not_Save")); }
                auto* box = dialog->findChild<QDialogButtonBox*>(QStringLiteral("spaceradPartButtons"));
                if (box) { cancelledEdit = true; box->button(QDialogButtonBox::Cancel)->click(); }
            }
        });
        if (editButton) editButton->click();
        report.check(cancelledEdit && widget.project().toJson().at("volumes") == edited.toJson().at("volumes"),
                     QStringLiteral("Cancel leaves edited geometry unchanged"));

        const auto busId = QStringLiteral("Bus_Al");
        row = volumeRow(volumes, busId);
        if (row >= 0) volumes->setCurrentCell(row, 0);
        const auto beforeDuplicate = widget.project().toJson().at("volumes");
        const bool duplicateHandled = runPartAction(widget, "spaceradEditPart", QStringLiteral("Deck_Al"), true);
        report.check(duplicateHandled && widget.project().toJson().at("volumes") == beforeDuplicate,
                     QStringLiteral("Duplicate ID keeps dialog open with inline error; cancel preserves project"));

        row = volumeRow(volumes, QStringLiteral("Edited_Si"));
        if (row >= 0) volumes->setCurrentCell(row, 0);
        if (auto* remove = button(&widget, "spaceradRemovePart")) remove->click();
        report.check(widget.project().toJson().at("volumes") == originalVolumes,
                     QStringLiteral("Removing selected added part restores original geometry"));

        const bool newTargetAdded = runPartAction(widget, "spaceradAddPart", QStringLiteral("New_Target"));
        auto authored = widget.project();
        report.check(newTargetAdded && authored.volumes.size() == original.volumes.size() + 1,
                     QStringLiteral("Add target for transport"));
        authored.events = 10000;
        for (auto& volume : authored.volumes)
            if (volume.id == "New_Target") volume.visible = false;
        widget.setProject(authored);
        const Json hiddenProject = widget.project().toJson();
        try {
            widget.selectComponent(QStringLiteral("New_Target"));
            report.check(widget.selectedComponent() == QStringLiteral("New_Target") &&
                             widget.property("selectedComponentId").toString() == QStringLiteral("New_Target") &&
                             volumes->currentRow() == volumeRow(volumes, QStringLiteral("New_Target")) &&
                             widget.project().toJson() == hiddenProject &&
                             !widget.project().volumes.back().visible,
                         QStringLiteral("Hidden component selection synchronizes volume row without mutation"));
        } catch (const std::exception& e) {
            report.check(false, QStringLiteral("Hidden component remains selectable"), QString::fromUtf8(e.what()));
        }
        const auto selectedBeforeUnknown = widget.selectedComponent();
        bool unknownRejected = false;
        try { widget.selectComponent(QStringLiteral("Unknown_Component")); }
        catch (const std::exception&) { unknownRejected = true; }
        report.check(unknownRejected && widget.selectedComponent() == selectedBeforeUnknown,
                     QStringLiteral("Unknown component selection is rejected"));

        const QString savedProject = output + QStringLiteral("/roundtrip.json");
        widget.saveProject(savedProject);
        WorkbenchWidget reopened;
        reopened.setProject(Project::assemblyDemo());
        reopened.loadProject(savedProject);
        report.check(reopened.project().toJson().at("volumes") == widget.project().toJson().at("volumes"),
                     QStringLiteral("Save and reopen preserves authored volume geometry"));

        const QString runDirectory = output + QStringLiteral("/transport-run");
        bool resultSignal = false;
        bool failedSignal = false;
        QString runError;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&widget, &WorkbenchWidget::resultReady, &loop, [&](const QByteArray&) {
            resultSignal = true;
            QTimer::singleShot(0, &loop, &QEventLoop::quit);
        });
        QObject::connect(widget.service(), &spacerad::AnalysisService::failed, &loop,
                         [&](const QString& message) { failedSignal = true; runError = message; loop.quit(); });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        try {
            widget.runTo(runDirectory);
            report.check(widget.service()->running() && !button(&widget, "spaceradAddPart")->isEnabled() &&
                             !button(&widget, "spaceradNewProject")->isEnabled(),
                         QStringLiteral("Run disables authoring and New Project while busy"));
            timeout.start(60000);
            loop.exec();
        } catch (const std::exception& e) {
            failedSignal = true;
            runError = QString::fromUtf8(e.what());
        }
        report.check(resultSignal && !failedSignal && widget.service()->state() == QStringLiteral("completed") &&
                         !widget.acceptedResult().isEmpty(),
                     QStringLiteral("Transport completed and Workbench accepted the result"), runError);
        if (!widget.acceptedResult().isEmpty()) {
            const Json result = Json::parse(widget.acceptedResult().toStdString());
            bool targetFound = false;
            bool validMassAndDose = true;
            for (const auto& target : result.at("targets")) {
                if (target.at("id") == "New_Target") {
                    targetFound = true;
                    validMassAndDose = target.at("mass_kg").get<double>() > 0.0 &&
                                       target.at("tid_Gy").get<double>() >= 0.0;
                }
            }
            report.check(result.at("targets").size() == 5 && targetFound && validMassAndDose,
                         QStringLiteral("Result contains five targets including New_Target with valid mass and dose"));
            const auto completedProject = widget.project().toJson();
            widget.selectComponent(QString{});
            const bool completedImageSaved = widget.grab().save(output + QStringLiteral("/completed.png"));
            report.check(completedImageSaved, QStringLiteral("Completed workbench screenshot saved"));
            int targetRow = volumeRow(results, QStringLiteral("New_Target"));
            bool focusSignal = false;
            QObject::connect(&widget, &WorkbenchWidget::componentSelected, &widget,
                             [&](const QString& id) { if (id == QStringLiteral("New_Target")) focusSignal = true; });
            if (targetRow >= 0) results->setCurrentCell(targetRow, 0);
            QApplication::processEvents();
            report.check(targetRow >= 0 && focusSignal && widget.selectedComponent() == QStringLiteral("New_Target") &&
                             volumes->currentRow() == volumeRow(volumes, QStringLiteral("New_Target")),
                         QStringLiteral("Selecting result row focuses target without mutation"));
            widget.selectComponent(QStringLiteral("Bus_Al"));
            report.check(results->currentRow()==-1&&volumes->currentRow()==volumeRow(volumes,QStringLiteral("Bus_Al")),QStringLiteral("Non-sensitive selection clears stale result current"));
            if(targetRow>=0)results->setCurrentCell(targetRow,0);
            report.check(widget.selectedComponent()==QStringLiteral("New_Target"),QStringLiteral("Returning to the previous result row focuses its component"));
            report.check(widget.project().toJson() == completedProject &&
                             !widget.acceptedResult().isEmpty() && Json::parse(widget.acceptedResult().toStdString()) == result,
                         QStringLiteral("Result-row focus preserves accepted result and project"));
            const bool focusedImageSaved = widget.grab().save(output + QStringLiteral("/focused.png"));
            report.check(focusedImageSaved, QStringLiteral("Focused target screenshot saved"));

            auto* histories = widget.findChild<QSpinBox*>(QStringLiteral("spaceradHistories"));
            const int resultsBeforeChange = results->rowCount();
            const bool acceptedBeforeChange = !widget.acceptedResult().isEmpty();
            const QString selectedBeforeChange = widget.selectedComponent();
            if (histories) histories->setValue(histories->value() + 1);
            bool sameSelectionCallSucceeded = true;
            try { widget.selectComponent(QStringLiteral("New_Target")); }
            catch (...) { sameSelectionCallSucceeded = false; }
            report.check(histories && resultsBeforeChange == 5 && acceptedBeforeChange &&
                             results->rowCount() == 0 && widget.acceptedResult().isEmpty() &&
                             selectedBeforeChange == QStringLiteral("New_Target") &&
                             widget.selectedComponent() == QStringLiteral("New_Target") &&
                             volumes->currentRow() == volumeRow(volumes, QStringLiteral("New_Target")) &&
                             sameSelectionCallSucceeded,
                         QStringLiteral("Changing histories clears stale results and preserves repeatable selection"));
        }
        saveSummary(report, output);
        return report.failures.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        saveSummary(report, output, QString::fromUtf8(e.what()));
        std::cerr << "ERROR " << e.what() << std::endl;
        return 1;
    }
}
