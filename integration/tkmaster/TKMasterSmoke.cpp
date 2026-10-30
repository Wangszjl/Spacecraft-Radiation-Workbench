#include "ui/TKMMainWindow.h"
#include "core/TKMProjectManager.h"
#include "radiation/RadiationProject.h"
#include "TKMSpaceRadAdapter.h"
#include <spacerad/WorkbenchWidget.h>
#include <QApplication>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <memory>
#include <stdexcept>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <type_traits>
int main(int argc,char** argv){
 static_assert(!std::is_same_v<scdat::radiation::Project,spacerad::Project>,"Host and SDK engine types must be isolated");
 QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QApplication app(argc,argv);if(argc!=3)return 2;QString runtime=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),out=QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();if(QFileInfo::exists(out)||!QDir().mkpath(out))return 2;
 TKMMainWindow host;host.show();QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();auto old=scdat::radiation::Project::assemblyDemo();old.events=10000;auto original=QByteArray::fromStdString(old.toJson().dump());auto before=QJsonDocument(host.projectManager()->collectSceneState()).toJson(QJsonDocument::Compact);auto checks=std::make_shared<spacerad::Json>(spacerad::Json::array());auto finish=[&](bool pass){QFile f(out+"/summary.json");if(f.open(QIODevice::WriteOnly))f.write(spacerad::Json({{"passed",pass},{"checks",*checks}}).dump(2).c_str());app.exit(pass?0:3);};auto check=[&](const char* name,bool pass){checks->push_back({{"name",name},{"passed",pass}});if(!pass)throw std::runtime_error(name);};
 try{auto* dock=openTKMSpaceRadDock(&host,{},original);auto* widget=dock->findChild<spacerad::WorkbenchWidget*>();QObject::connect(widget,&spacerad::WorkbenchWidget::resultReady,&host,[&,dock,widget](const QByteArray& result){QTimer::singleShot(0,&host,[&,dock,widget,result]{try{check("actual TKMaster dock accepts Geant4 result",spacerad::Json::parse(result.toStdString()).at("histories")==10000);check("result returned to host property",host.property("spaceRadResultJson").toByteArray()==result);auto after=QJsonDocument(host.projectManager()->collectSceneState()).toJson(QJsonDocument::Compact);QFile beforeFile(out+"/scene-before.json");beforeFile.open(QIODevice::WriteOnly);beforeFile.write(before);beforeFile.close();QFile afterFile(out+"/scene-after.json");afterFile.open(QIODevice::WriteOnly);afterFile.write(after);afterFile.close();check("host scene unchanged",before==after);check("old radiation project unchanged",original==QByteArray::fromStdString(old.toJson().dump()));QApplication::processEvents();check("actual host screenshot",host.grab().save(out+"/host.png"));check("embedded dock screenshot",dock->grab().save(out+"/dock.png"));finish(true);}catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);}});});QObject::connect(widget->service(),&spacerad::AnalysisService::failed,&host,[&](const QString& message){checks->push_back({{"name",message.toStdString()},{"passed",false}});finish(false);});QTimer::singleShot(500,&host,[&,widget]{try{widget->runTo(out+"/run");}catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);}});}catch(const std::exception& e){checks->push_back({{"name",e.what()},{"passed",false}});finish(false);return 3;}
 QTimer::singleShot(60000,&host,[&]{checks->push_back({{"name","host integration timeout"},{"passed",false}});finish(false);});return app.exec();
}
