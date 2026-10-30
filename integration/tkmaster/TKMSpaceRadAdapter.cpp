#include "TKMSpaceRadAdapter.h"
#include <spacerad/WorkbenchWidget.h>
#include <QDockWidget>
#include <QCoreApplication>
#include <QFile>
#include <QMainWindow>
#include <QMessageBox>
QDockWidget* openTKMSpaceRadDock(QMainWindow* host,const QString& runtimeFile,const QByteArray& projectJson){
 auto* dock=new QDockWidget(QObject::tr("SpaceRad 辐射分析组件"),host);dock->setObjectName("spaceRadDock");dock->setAttribute(Qt::WA_DeleteOnClose);auto* widget=new spacerad::WorkbenchWidget(dock);dock->setWidget(widget);
 try{auto path=runtimeFile.isEmpty()?qEnvironmentVariable("SPACERAD_RUNTIME_FILE"):runtimeFile;if(path.isEmpty()&&QFile::exists(QCoreApplication::applicationDirPath()+"/spacerad-runtime.json"))path=QCoreApplication::applicationDirPath()+"/spacerad-runtime.json";if(!path.isEmpty())widget->setRuntime(spacerad::loadRuntimeFile(path));if(!projectJson.isEmpty())widget->setProject(spacerad::Project::fromJson(spacerad::Json::parse(projectJson.toStdString())));}catch(...){delete dock;throw;}
 QObject::connect(widget,&spacerad::WorkbenchWidget::resultReady,host,[host](const QByteArray& result){host->setProperty("spaceRadResultJson",result);});
 host->addDockWidget(Qt::RightDockWidgetArea,dock);dock->setFloating(true);dock->resize(1250,1000);dock->show();return dock;
}
