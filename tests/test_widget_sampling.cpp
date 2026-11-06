#include "spacerad/WorkbenchWidget.h"
#include "spacerad/ImportanceConfig.h"
#include "spacerad/VoxelScoring.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
namespace {
Json checks=Json::array();
void check(bool ok,const char* name){checks.push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);}
void edit(WorkbenchWidget& widget,const std::function<void(QDialog&)>& action){
    bool opened=false;
    std::exception_ptr failure;
    QTimer::singleShot(80,&widget,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)return;
        opened=true;
        try{action(*dialog);}catch(...){failure=std::current_exception();dialog->reject();}
    });
    QTimer deadline;deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&widget,[]{if(auto* dialog=QApplication::activeModalWidget())dialog->close();});
    deadline.start(8000);widget.findChild<QPushButton*>("spaceradConfigureImportance")->click();
    if(failure)std::rethrow_exception(failure);
    check(opened,"Native importance settings dialog opens");
}
void accept(QDialog& d){
    d.findChild<QDialogButtonBox*>("spaceradSamplingButtons")->button(QDialogButtonBox::Ok)->click();
}
}
int main(int argc,char** argv){
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc,argv);
    if(argc!=4)return 2;
    const QString projectPath=QString::fromLocal8Bit(argv[1]),runtimePath=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
    if(QDir(output).exists()||!QDir().mkpath(output))return 2;
    bool passed=false;
    try{
        WorkbenchWidget widget;widget.resize(1180,980);widget.show();widget.loadProject(projectPath);app.processEvents();
        widget.findChild<QTabWidget*>()->setCurrentIndex(2);
        const auto original=widget.project();
        check(original.spectra.size()==2,"Reference project contains electron and proton spectra");
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);
            d.findChild<QLineEdit*>("spaceradSamplingPhysical")->setText("0.4");
            d.reject();
        });
        check(widget.project().toJson()==original.toJson(),"Cancel keeps the full project unchanged");
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);
            d.findChild<QLineEdit*>("spaceradSamplingTail")->setText("0");
            accept(d);
            check(d.isVisible()&&!d.findChild<QLabel*>("spaceradSamplingError")->text().isEmpty(),"Invalid tail probability is rejected in the dialog");
            d.reject();
        });
        check(widget.project().toJson()==original.toJson(),"Invalid settings do not modify the project");
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);
            d.findChild<QCheckBox*>("spaceradSamplingCustomSpecies")->setChecked(true);
            auto* table=d.findChild<QTableWidget*>("spaceradSamplingSpecies");
            for(int row=0;row<table->rowCount();++row)table->item(row,2)->setText("0.9");
            accept(d);
            check(d.isVisible()&&!d.findChild<QLabel*>("spaceradSamplingError")->text().isEmpty(),
                  "Proposal fractions that do not sum to one are rejected without closing");
            d.reject();
        });
        auto voxel=configureVoxelScoring(original,"OBC_Si",{2,1,1},{0,1,10});
        widget.setProject(voxel);
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);accept(d);
            check(d.isVisible()&&!d.findChild<QLabel*>("spaceradSamplingError")->text().isEmpty(),
                  "Incompatible voxel mode is rejected without silently disabling it");d.reject();
        });
        check(widget.project().toJson()==voxel.toJson(),"Rejected importance edit preserves voxel settings");
        widget.setProject(original);
        const double physical=.20000000000000004,tail=.0010000000000000011;
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);
            d.findChild<QLineEdit*>("spaceradSamplingPhysical")->setText(QString::number(physical,'g',17));
            d.findChild<QLineEdit*>("spaceradSamplingTail")->setText(QString::number(tail,'g',17));
            d.findChild<QCheckBox*>("spaceradSamplingCustomSpecies")->setChecked(true);
            auto* table=d.findChild<QTableWidget*>("spaceradSamplingSpecies");
            for(int i=0;i<table->rowCount();++i)table->item(i,2)->setText(table->item(i,0)->text()=="e-"?"0.35":"0.65");
            d.grab().save(output+"/editor.png");accept(d);
        });
        auto weighted=widget.project();
        check(weighted.toJson().at("schema")=="scdat.radiation/8"&&
              weighted.sourceSampling.at("physical_fraction")==physical&&weighted.sourceSampling.at("tail_fraction")==tail,
              "Native settings preserve parameter precision and persist schema v8");
        auto expected=configureImportanceSampling(original,physical,tail,{{"e-",.35},{"proton",.65}});
        check(weighted.toJson()==expected.toJson(),"UI and public configuration API produce the same project");
        check(weighted.expectedPrimaries()==original.expectedPrimaries()&&weighted.environmentMetadata==original.environmentMetadata&&
              weighted.environmentSource==original.environmentSource,"Physical exposure and orbit provenance are preserved");
        widget.saveProject(output+"/weighted-project.json");widget.loadProject(output+"/weighted-project.json");
        check(widget.project().sourceSampling==weighted.sourceSampling,"Saved sampling settings reopen unchanged");
        edit(widget,[&](QDialog& d){
            check(d.findChild<QLineEdit*>("spaceradSamplingPhysical")->text().toDouble()==physical&&
                  d.findChild<QLineEdit*>("spaceradSamplingTail")->text().toDouble()==tail,
                  "Reopened editor retains precise settings");accept(d);
        });
        widget.setRuntime(loadRuntimeFile(runtimePath));
        auto live=widget.project();live.events=8000;live.seed=346781;widget.setProject(live);
        QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        QObject::connect(&widget,&WorkbenchWidget::resultReady,&loop,[&](const QByteArray&){loop.quit();});
        QObject::connect(widget.service(),&AnalysisService::failed,&loop,[&](const QString&){loop.quit();});
        deadline.start(120000);widget.runTo(output+"/job");
        check(!widget.findChild<QPushButton*>("spaceradConfigureImportance")->isEnabled(),"Settings are disabled during transport");
        loop.exec();
        check(!widget.acceptedResult().isEmpty(),"The UI-configured proposal completes real Geant4 transport");
        const auto result=widget.acceptedResult();
        edit(widget,[&](QDialog& d){
            d.findChild<QLineEdit*>("spaceradSamplingPhysical")->setText("0.9");
            d.findChild<QLineEdit*>("spaceradSamplingTail")->setText("0.05");
            auto* table=d.findChild<QTableWidget*>("spaceradSamplingSpecies");
            for(int row=0;row<table->rowCount();++row)table->item(row,2)->setText(table->item(row,0)->text()=="e-"?"0.15":"0.85");
            d.grab().save(output+"/readable-editor.png");d.reject();
        });
        check(widget.acceptedResult()==result,"Cancelling settings preserves an accepted result");
        edit(widget,[&](QDialog& d){d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(false);accept(d);});
        check(widget.project().sourceSampling.is_null()&&widget.acceptedResult().isEmpty()&&
              !widget.findChild<QPushButton*>("spaceradImportanceView")->isEnabled(),"Disabling sampling clears the stale result and detail action");
        edit(widget,[&](QDialog& d){
            d.findChild<QCheckBox*>("spaceradSamplingEnabled")->setChecked(true);
            widget.setProject(Project::demo());accept(d);
        });
        check(widget.project().sourceSampling.is_null()&&widget.project().name==Project::demo().name,
              "Host replacement during the modal editor rejects the stale edit");
        check(widget.findChild<QLabel*>("spaceradStatus")->text().contains("Project changed"),"Stale-edit rejection is explained");
        passed=true;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    QFile summary(output+"/summary.json");summary.open(QIODevice::WriteOnly);
    summary.write(Json{{"passed",passed},{"checks",checks}}.dump(2).c_str());
    std::cout<<checks.size()<<" native sampling configuration checks; passed="<<passed<<'\n';
    return passed?0:1;
}
