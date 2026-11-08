#include <spacerad/WorkbenchWidget.h>
#include "runtime/ProjectFile.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>

using namespace spacerad;
namespace {
int checks=0;
void check(bool condition,const char* label){
    if(!condition)throw std::runtime_error(label);
    ++checks;std::cout<<"PASS "<<label<<'\n';
}
template<class T> T* child(QWidget& w,const char* name){
    auto* result=w.findChild<T*>(name);
    if(!result)throw std::runtime_error(std::string("Missing widget: ")+name);
    return result;
}
void write(const QString& path,const QByteArray& bytes){
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes)!=bytes.size())
        throw std::runtime_error("Cannot write fixture");
}
}
int main(int argc,char** argv){
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc,argv);
    if(argc!=4)return 2;
    try{
        const auto out=QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath();
        if(QFileInfo::exists(out)||!QDir().mkpath(out))return 2;
        auto p=engine::loadProjectFile(QString::fromLocal8Bit(argv[2])).project;
        p.events=5000;
        WorkbenchWidget host;host.setProject(p);host.setRuntime(loadRuntimeFile(QString::fromLocal8Bit(argv[1])));
        host.resize(1180,850);host.show();app.processEvents();
        const auto before=host.project().toJson();
        bool invalidRejected=false;
        const auto job=out+"/job";
        const auto response=out+"/response.csv";
        write(response,"device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference\n"
                       "Target,ddd,proton,9,2,MeV_cm2_per_g,isotropic_omnidirectional,Synthetic fixture\n"
                       "Target,ddd,proton,12,2,MeV_cm2_per_g,isotropic_omnidirectional,Synthetic fixture\n");
        QTimer::singleShot(0,&host,[&]{
            auto* setup=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!setup||setup->objectName()!="spaceradTargetFluenceSetup"){if(setup)setup->reject();return;}
            auto* edges=child<QLineEdit>(*setup,"spaceradTargetFluenceEdges");
            auto* buttons=setup->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            edges->setText("12, 9");
            buttons->click();
            invalidRejected=setup->isVisible()&&!child<QLabel>(*setup,"spaceradTargetFluenceSetupStatus")->text().isEmpty();
            edges->setText("9, 10.5, 12");
            child<QLineEdit>(*setup,"spaceradTargetFluenceOutput")->setText(job);
            child<QLineEdit>(*setup,"spaceradHistoryResponseInput")->setText(response);
            buttons->click();
            if(setup->isVisible())setup->reject();
        });
        QEventLoop loop;QByteArray accepted;QString error;
        QObject::connect(&host,&WorkbenchWidget::resultReady,&loop,[&](const QByteArray& raw){accepted=raw;loop.quit();});
        QObject::connect(host.service(),&AnalysisService::failed,&loop,[&](const QString& e){error=e;loop.quit();});
        QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{error="Transport timed out";host.cancel();loop.quit();});
        child<QPushButton>(host,"spaceradTargetFluenceRun")->click();
        check(invalidRejected,"Setup rejects descending energy boundaries");
        check(host.service()->running(),"Desktop starts real fluence Worker");
        deadline.start(60000);loop.exec();
        if(!error.isEmpty())throw std::runtime_error(error.toStdString());
        check(!accepted.isEmpty()&&Json::parse(accepted.toStdString()).contains("target_fluence"),"Desktop accepted verified target fluence");
        check(host.project().toJson()==before,"Scoring settings do not modify the live project");
        child<QPushButton>(host,"spaceradTargetFluenceView")->click();
        auto* view=child<QDialog>(host,"spaceradTargetFluenceReview");
        check(child<QLineEdit>(*view,"spaceradTargetFluenceArchive")->text()==job,"Viewer prefills the accepted archive");
        check(child<QTableWidget>(*view,"spaceradTargetFluenceBins")->rowCount()==26,"Viewer displays six species with underflow and overflow plus total and other");
        const auto score=Json::parse(accepted.toStdString()).at("target_fluence").at("history_response");
        check(score.at("standard_error").is_number()&&score.at("standard_error")>0,"Desktop archive contains a positive history response standard error");
        check(child<QPushButton>(*view,"spaceradTargetFluenceExport")->isEnabled()&&
              child<QPlainTextEdit>(*view,"spaceradTargetFluenceFolded")->toPlainText().contains("MC"),
              "Viewer displays pinned history response uncertainty without reopening the CSV");
        check(view->grab().save(out+"/review.png"),"Review screenshot saved");
        child<QLineEdit>(*view,"spaceradTargetFluenceResponse")->setText(response);
        child<QPushButton>(*view,"spaceradTargetFluenceFold")->click();
        auto* exportButton=child<QPushButton>(*view,"spaceradTargetFluenceExport");
        check(exportButton->isEnabled()&&!child<QPlainTextEdit>(*view,"spaceradTargetFluenceFolded")->toPlainText().isEmpty(),"Desktop folds the verified local fluence");
        app.processEvents();
        child<QLineEdit>(*view,"spaceradTargetFluenceResponse")->setText(out+"/missing.csv");
        check(!exportButton->isEnabled(),"Changing response clears stale export");
        child<QPushButton>(*view,"spaceradTargetFluenceFold")->click();
        check(!exportButton->isEnabled(),"Invalid response leaves no exportable result");
        child<QLineEdit>(*view,"spaceradTargetFluenceArchive")->setText(out+"/missing-job");
        check(child<QTableWidget>(*view,"spaceradTargetFluenceBins")->rowCount()==0,"Changing archive clears stale bins");
        std::cout<<checks<<" native target-fluence checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
