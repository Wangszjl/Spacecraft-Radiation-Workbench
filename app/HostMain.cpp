#include "HostMain.h"
#include "spacerad/WorkbenchWidget.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>
int runSpaceRadHost(int argc,char** argv,bool embedded){
 QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QApplication app(argc,argv);app.setApplicationName("SpaceRad");app.setOrganizationName("SpaceRad");QMainWindow window;window.setWindowTitle(embedded?"SpaceRad · 宿主嵌入示例":"SpaceRad · 航天器辐射分析");window.resize(1260,1000);
 auto* widget=new spacerad::WorkbenchWidget(&window);QString hostMarker="HOST_PROJECT_UNCHANGED";QLabel* marker=nullptr;
 if(embedded){auto* tabs=new QTabWidget(&window);auto* home=new QWidget(tabs);auto* layout=new QVBoxLayout(home);marker=new QLabel(hostMarker,home);layout->addWidget(marker);layout->addWidget(new QLabel(QObject::tr("这是独立宿主示例。辐射页面共用公开 WorkbenchWidget，不链接 Geant4。"),home));tabs->addTab(home,QObject::tr("宿主工程"));tabs->addTab(widget,QObject::tr("辐射分析"));tabs->setCurrentWidget(widget);window.setCentralWidget(tabs);}else window.setCentralWidget(widget);
 auto args=app.arguments();auto option=[&](const QString& name){int i=args.indexOf(name);return i>=0&&i+1<args.size()?args[i+1]:QString{};};
 try{auto runtime=option("--runtime");if(runtime.isEmpty()&&QFile::exists(QCoreApplication::applicationDirPath()+"/runtime.json"))runtime=QCoreApplication::applicationDirPath()+"/runtime.json";if(!runtime.isEmpty())widget->setRuntime(spacerad::loadRuntimeFile(runtime));auto project=option("--project");if(!project.isEmpty())widget->loadProject(project);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 window.show();auto smoke=option("--smoke");
 if(!smoke.isEmpty()){
  auto out=QFileInfo(smoke).absoluteFilePath();if(QFileInfo::exists(out)||!QDir().mkpath(out)){std::cerr<<"Choose a new smoke directory\n";return 2;}
  auto checks=std::make_shared<spacerad::Json>(spacerad::Json::array());auto phase=std::make_shared<int>(0);auto finish=[&,out,checks](bool passed){QFile f(out+"/summary.json");if(f.open(QIODevice::WriteOnly))f.write(spacerad::Json({{"passed",passed},{"embedded",embedded},{"checks",*checks}}).dump(2).c_str());app.exit(passed?0:3);};auto check=[checks](const char* name,bool ok){checks->push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);};
  QObject::connect(widget,&spacerad::WorkbenchWidget::resultReady,&window,[&,phase,out,checks,check,finish](const QByteArray& bytes){QTimer::singleShot(0,&window,[&,phase,out,checks,check,finish,bytes]{try{
   auto r=spacerad::Json::parse(bytes.toStdString());check("real seeded ten-thousand-history transport",r.at("histories")==10000&&r.at("schema")=="scdat.radiation-result/2");check("visible accepted target table",widget->findChild<QTableWidget*>("spaceradResults")->rowCount()==int(r.at("targets").size())&&!widget->acceptedResult().isEmpty());QApplication::processEvents();check("completed screenshot",window.grab().save(out+"/completed.png"));if(marker)check("host model marker preserved",marker->text()==hostMarker);
   auto p=widget->project();p.events=1000000;widget->setProject(p);check("project replacement clears old results",widget->acceptedResult().isEmpty()&&widget->findChild<QTableWidget*>("spaceradResults")->rowCount()==0);*phase=1;widget->runTo(out+"/cancelled");QTimer::singleShot(100,widget,&spacerad::WorkbenchWidget::cancel);
  }catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);}});});
  QObject::connect(widget->service(),&spacerad::AnalysisService::stateChanged,&window,[&,phase,out,checks,check,finish](const QString& state){if(state!="cancelled"||*phase!=1)return;try{check("cancelled job has no accepted result",widget->acceptedResult().isEmpty()&&widget->service()->result().isEmpty());auto* closing=new spacerad::WorkbenchWidget(&window);closing->setRuntime(widget->service()->runtime());auto closingProject=spacerad::Project::demo();closingProject.events=1000000;closing->setProject(closingProject);closing->runTo(out+"/close-running");delete closing;check("destroying a running widget returns safely",true);if(marker)check("host marker preserved after cancellation",marker->text()==hostMarker);finish(true);}catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);}});
  QObject::connect(widget->service(),&spacerad::AnalysisService::failed,&window,[=](const QString& message){checks->push_back({{"name","Worker failure"},{"error",message.toStdString()},{"passed",false}});finish(false);});
  QTimer::singleShot(500,&window,[&,out,checks,check,finish]{try{auto p=spacerad::Project::assemblyDemo();auto exact=p;exact.events=2000000;exact.duration=86400.12345678901;exact.cut=.000012345678901234;widget->setProject(exact);auto recovered=widget->project();check("legal parameters round-trip without clamping",recovered.events==exact.events&&recovered.duration==exact.duration&&recovered.cut==exact.cut);p.events=10000;widget->setProject(p);QApplication::processEvents();check("initial model screenshot",window.grab().save(out+"/initial.png"));widget->saveProject(out+"/saved-project.json");widget->loadProject(out+"/saved-project.json");check("project reopen keeps geometry",widget->project().volumes.size()==p.volumes.size());widget->runTo(out+"/run");}catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);}});
  QTimer::singleShot(60000,&window,[=]{checks->push_back({{"name","native smoke timeout"},{"passed",false}});finish(false);});
 }
 return app.exec();
}
