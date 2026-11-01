#include <spacerad/WorkbenchWidget.h>
#include <spacerad/ShieldingScan.h>
#include "runtime/ProjectFile.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
namespace {
Json checks=Json::array();
void check(bool value,const char* name){checks.push_back({{"name",name},{"passed",value}});if(!value)throw std::runtime_error(name);}
QPushButton* button(QWidget& w,const char* name){return w.findChild<QPushButton*>(name);}
void exportFile(QDialog& dialog,const char* action,const QString& path){
    bool selected=false;QTimer picker;picker.setSingleShot(true);
    QObject::connect(&picker,&QTimer::timeout,&dialog,[&]{if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){selected=true;file->setDirectory(QFileInfo(path).absolutePath());file->selectFile(QFileInfo(path).fileName());if(auto* name=file->findChild<QLineEdit*>("fileNameEdit"))name->setText(QFileInfo(path).fileName());QTimer::singleShot(100,file,[file]{QMetaObject::invokeMethod(file,"accept");});}});
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&dialog,[]{if(auto* w=QApplication::activeModalWidget())w->close();});watchdog.start(5000);picker.start(50);button(dialog,action)->click();
    check(selected&&QFile::exists(path),"Scan exports through actual file picker");
}
void run(QDialog& dialog,bool cancel){
    auto* service=dialog.findChild<ShieldingScanService*>();QEventLoop loop;QString error;
    auto completed=QObject::connect(service,&ShieldingScanService::completed,&loop,&QEventLoop::quit);
    auto failed=QObject::connect(service,&ShieldingScanService::failed,&loop,[&](const QString& e){error=e;loop.quit();});
    auto state=QObject::connect(service,&ShieldingScanService::stateChanged,&loop,[&](const QString& s){if(s=="cancelled")loop.quit();});
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&loop,[&]{error="Scan UI timed out";service->cancel();loop.quit();});watchdog.start(60000);
    button(dialog,"spaceradScanRun")->click();
    check(service->running()&&!button(dialog,"spaceradScanRun")->isEnabled()&&!dialog.findChild<QSpinBox*>("spaceradScanHistories")->isEnabled(),"Scan settings are disabled during transport");
    if(cancel)QTimer::singleShot(50,&dialog,[&]{button(dialog,"spaceradScanCancel")->click();});
    loop.exec();QObject::disconnect(completed);QObject::disconnect(failed);QObject::disconnect(state);
    if(!error.isEmpty())throw std::runtime_error(error.toStdString());
    check(service->state()==(cancel?"cancelled":"completed"),cancel?"Native scan cancel reaches terminal state":"Native scan completes real transport");
}
}
int main(int argc,char** argv){
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);
    if(argc!=4)return 2;const auto runtime=QString::fromLocal8Bit(argv[1]),input=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);
    if(QFileInfo::exists(out)||!QDir().mkpath(out))return 2;bool passed=false;
    try {
        WorkbenchWidget host;host.resize(1150,850);host.setRuntime(loadRuntimeFile(runtime));host.setProject(engine::loadProjectFile(input).project);host.show();app.processEvents();
        const auto before=host.project().toJson();const auto accepted=host.acceptedResult();int changed=0,ready=0;
        QObject::connect(&host,&WorkbenchWidget::projectChanged,&host,[&]{++changed;});QObject::connect(&host,&WorkbenchWidget::resultReady,&host,[&](const QByteArray&){++ready;});
        button(host,"spaceradShieldingScan")->click();app.processEvents();
        auto* dialog=host.findChild<QDialog*>("spaceradShieldingDialog");check(dialog&&dialog->isVisible()&&!dialog->isModal(),"Shared workbench opens independent scan dialog");
        auto* thickness=dialog->findChild<QLineEdit*>("spaceradScanThicknesses");auto* histories=dialog->findChild<QSpinBox*>("spaceradScanHistories");auto* output=dialog->findChild<QLineEdit*>("spaceradScanOutput");
        thickness->setText("0, 0.5, 2");histories->setValue(2000);output->setText(out+"/job");
        check(dialog->findChild<QLabel*>("spaceradScanPreview")->text().contains("12000"),"Plan preview accounts for species and fixed histories");
        run(*dialog,false);auto* service=dialog->findChild<ShieldingScanService*>();const auto report=Json::parse(service->result().toStdString());
        check(report.at("cases").size()==6&&report.at("curves").size()==3&&dialog->findChild<QTableWidget*>("spaceradScanTable")->rowCount()==3,"Native completed scan displays three thickness rows");
        check(dialog->findChild<QProgressBar*>()->value()==1000,"Native complete scan reaches full progress");
        exportFile(*dialog,"spaceradScanCsv",out+"/export.csv");exportFile(*dialog,"spaceradScanJson",out+"/export.json");
        QFile exported(out+"/export.json");exported.open(QIODevice::ReadOnly);check(Json::parse(exported.readAll().toStdString())==report,"Scan JSON export retains report and provenance");
        QFile exportedCsv(out+"/export.csv");exportedCsv.open(QIODevice::ReadOnly);check(exportedCsv.readAll()==shieldingScanCsv(report),"Scan CSV export retains sums and species components");
        check(dialog->grab().save(out+"/scan.png"),"Native scan window screenshot");
        output->setText(out+"/cancelled");histories->setValue(100000000);run(*dialog,true);
        check(service->result().isEmpty()&&loadShieldingScan(out+"/cancelled").at("status")=="cancelled","Cancelled UI scan retains terminal archive without full accepted result");
        check(host.project().toJson()==before&&host.acceptedResult()==accepted&&changed==0&&ready==0&&!host.service()->running(),"Scan workflow preserves host project, accepted result and signals");
        QPointer<QDialog> old=dialog;dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();check(old.isNull(),"Scan dialog is destroyed on close");
        host.setRuntime({});host.showShieldingScan(out+"/job");app.processEvents();auto* historical=host.findChild<QDialog*>("spaceradShieldingDialog");
        check(historical&&historical->findChild<QTableWidget*>("spaceradScanTable")->rowCount()==3&&!historical->findChild<ShieldingScanService*>()->running(),"Historical scan opens without Worker configuration");
        historical->findChild<ShieldingScanService*>()->configure(loadRuntimeFile(runtime));
        historical->findChild<QLineEdit*>("spaceradScanOutput")->setText(out+"/closed-active");historical->findChild<QLineEdit*>("spaceradScanThicknesses")->setText("0,0.5,2");historical->findChild<QSpinBox*>("spaceradScanHistories")->setValue(100000000);
        button(*historical,"spaceradScanRun")->click();QPointer<QDialog> closing=historical;QEventLoop closingLoop;
        QObject::connect(historical,&QObject::destroyed,&closingLoop,&QEventLoop::quit);QTimer::singleShot(50,&closingLoop,[&]{if(closing)closing->close();});QTimer::singleShot(5000,&closingLoop,&QEventLoop::quit);closingLoop.exec();
        check(closing.isNull()&&loadShieldingScan(out+"/closed-active").at("status")=="cancelled","Closing active scan destroys its service and records a cancelled archive");
        passed=true;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    QFile file(out+"/summary.json");file.open(QIODevice::WriteOnly);file.write(QByteArray::fromStdString(Json{{"passed",passed},{"checks",checks}}.dump(2)));
    std::cout<<checks.size()<<" native scan checks; passed="<<passed<<'\n';return passed?0:1;
}
