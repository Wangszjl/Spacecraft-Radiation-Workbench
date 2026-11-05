#include "spacerad/WorkbenchWidget.h"
#include "spacerad/ImportanceReview.h"
#include "spacerad/Results.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
void exportFile(QDialog& viewer,const char* action,const QString& filename){
    bool reached=false;
    QTimer::singleShot(50,&viewer,[&]{
        if(auto* modal=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
            reached=true;modal->setDirectory(QFileInfo(filename).absolutePath());modal->selectFile(QFileInfo(filename).fileName());
            if(auto* edit=modal->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(filename).fileName());
            QTimer::singleShot(100,modal,[modal]{QMetaObject::invokeMethod(modal,"accept");});
        }
    });
    QTimer timeout;timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&viewer,[&]{if(auto* modal=QApplication::activeModalWidget())modal->close();});
    timeout.start(5000);viewer.findChild<QPushButton*>(action)->click();
    check(reached&&QFile::exists(filename),"Viewer exports via its file picker");
}
}
int main(int argc,char** argv){
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    if(argc!=4)return 2;
    const auto archivePath=QString::fromLocal8Bit(argv[1]),runtimePath=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
    if(QFileInfo::exists(output)||!QDir().mkpath(output))return 2;
    bool passed=false;
    try{
        const auto archive=loadArchivedRun(archivePath);
        const auto expected=importanceReview(archive.project(),archive.result());
        WorkbenchWidget widget;widget.resize(1180,950);widget.show();app.processEvents();
        auto* current=widget.findChild<QPushButton*>("spaceradImportanceView");
        check(current&&!current->isEnabled(),"No detail action without an accepted weighted result");
        const auto original=widget.project().toJson();
        const auto accepted=widget.acceptedResult();
        int changed=0,ready=0;
        QObject::connect(&widget,&WorkbenchWidget::projectChanged,&widget,[&]{++changed;});
        QObject::connect(&widget,&WorkbenchWidget::resultReady,&widget,[&]{++ready;});
        widget.showResults(archivePath);app.processEvents();
        auto* history=widget.findChild<QDialog*>("spaceradResultsDialog");
        auto* open=history->findChild<QPushButton*>("spaceradArchiveImportanceA");
        check(open&&open->isEnabled()&&!history->findChild<QPushButton*>("spaceradArchiveImportanceB")->isEnabled(),
              "Loaded weighted archive enables only the selected side");
        open->click();app.processEvents();
        auto* viewer=history->findChild<QDialog*>("spaceradImportanceDialog");
        check(viewer&&viewer->isVisible()&&!viewer->isModal(),"Weighted archive opens a nonmodal detail snapshot");
        auto* targets=viewer->findChild<QTableWidget*>("spaceradImportanceTargets");
        auto* groups=viewer->findChild<QTableWidget*>("spaceradImportanceGroups");
        check(targets->rowCount()==int(expected.at("targets").size()),"Every sensitive target is shown");
        bool lowDespiteRse=false;
        for(int row=0;row<targets->rowCount();++row){
            const auto& target=expected.at("targets").at(row);
            check(targets->item(row,4)->data(Qt::UserRole).toString().toStdString()==target.at("status"),
                  "Displayed screen matches the shared API");
            if(target.at("precision_met")==true&&target.at("passes_screen")==false){
                targets->setCurrentCell(row,0);lowDespiteRse=true;
            }
        }
        check(lowDespiteRse,"Real archive demonstrates acceptable observed RSE with insufficient source support");
        check(groups->rowCount()==4,"Each target exposes two species and two energy regions");
        bool zero=false;for(int i=0;i<groups->rowCount();++i)zero|=groups->item(i,8)->data(Qt::UserRole)=="zero_hit";
        check(zero,"A real zero-hit source region remains explicit");
        check(viewer->findChild<QLabel*>("spaceradImportanceWarning")->text().contains(QString::fromUtf8("不可")),
              "Shared-history error aggregation warning is visible");
        exportFile(*viewer,"spaceradImportanceJson",output+"/review.json");
        QFile json(output+"/review.json");json.open(QIODevice::ReadOnly);
        check(Json::parse(json.readAll().toStdString())==expected,"Exported review equals API output at full precision");
        exportFile(*viewer,"spaceradImportanceCsv",output+"/regions.csv");
        QFile csv(output+"/regions.csv");csv.open(QIODevice::ReadOnly);
        check(csv.readAll().count('\n')==1+4*targets->rowCount(),"CSV contains each target/species/region");
        check(widget.project().toJson()==original&&widget.acceptedResult()==accepted&&changed==0&&ready==0&&!widget.service()->running(),
              "Historical review preserves host state and emits no analysis signals");
        check(viewer->grab().save(output+"/importance.png"),"Native statistics snapshot captured");
        history->findChild<QLineEdit*>("spaceradArchiveA")->setText(output+"/missing");
        history->findChild<QPushButton*>("spaceradArchiveLoadA")->click();
        check(!open->isEnabled()&&viewer->isVisible()&&targets->rowCount()==int(expected.at("targets").size()),
              "Invalid reselection clears the history action but preserves an open snapshot");
        delete history;app.processEvents();

        widget.setRuntime(loadRuntimeFile(runtimePath));
        auto p=archive.project();p.events=10000;p.seed=918273;
        widget.setProject(p);
        QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        QObject::connect(&widget,&WorkbenchWidget::resultReady,&loop,[&](const QByteArray&){loop.quit();});
        QObject::connect(widget.service(),&AnalysisService::failed,&loop,[&](const QString&){loop.quit();});
        deadline.start(120000);widget.runTo(output+"/live-job");loop.exec();
        check(!widget.acceptedResult().isEmpty()&&current->isEnabled(),"Real live weighted transport enables the current result detail action");
        const auto live=Json::parse(widget.acceptedResult().toStdString());
        const auto liveReview=importanceReview(Project::fromJson(live.at("project")),live);
        check(widget.findChild<QLabel*>("spaceradStatus")->text().contains(QString::fromUtf8("筛查")),
              "Live completion retains source-support screening status");
        current->click();app.processEvents();
        auto* liveViewer=widget.findChild<QDialog*>("spaceradImportanceDialog");
        check(liveViewer&&liveViewer->findChild<QTableWidget*>("spaceradImportanceTargets")->rowCount()==int(liveReview.at("targets").size()),
              "Current result detail displays the accepted launch snapshot");
        widget.setProject(Project::demo());
        check(!current->isEnabled()&&liveViewer->isVisible(),"Project replacement disables stale action while an opened snapshot remains readable");
        liveViewer->close();passed=true;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    QFile evidence(output+"/summary.json");evidence.open(QIODevice::WriteOnly);
    evidence.write(Json{{"passed",passed},{"checks",checks}}.dump(2).c_str());
    std::cout<<checks.size()<<" native importance checks; passed="<<passed<<'\n';
    return passed?0:1;
}
