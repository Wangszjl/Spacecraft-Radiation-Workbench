#include <spacerad/WorkbenchWidget.h>
#include <spacerad/DeviceResponse.h>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QVTKOpenGLNativeWidget.h>
#include "Geometry.h"
#include "runtime/ProjectFile.h"
#include <iostream>
using namespace spacerad;
namespace { void write(const QString& p,const QByteArray& b){QFile f(p);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(b)!=b.size())throw std::runtime_error("Could not write test response CSV");} }
int main(int argc,char** argv){
 QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);if(argc!=3)return 2;
 try{
  auto p=engine::loadProjectFile(QString::fromLocal8Bit(argv[1])).project;const auto out=QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();if(QFileInfo::exists(out)||!QDir().mkpath(out))return 2;
  QByteArray csv="device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference\n";std::vector<std::string> devices;for(const auto& volume:p.volumes)if(volume.sensitive)devices.push_back(volume.id);for(const auto& instance:p.instances){auto t=std::find_if(p.templates.begin(),p.templates.end(),[&](const auto& x){return x.id==instance.templateId;});if(t==p.templates.end())throw std::runtime_error("Missing template fixture");for(const auto& volume:t->volumes)if(volume.sensitive)devices.push_back(instance.id+"/"+volume.id);}size_t targets=devices.size();for(const auto& device:devices)for(const auto& spectrum:p.spectra){QString metric="ddd",unit="MeV_cm2_per_g";for(double energy:{spectrum.energy.front(),spectrum.energy.back()})csv+=(QString::fromStdString(device)+","+metric+","+QString::fromStdString(spectrum.particle)+","+QString::number(energy,'g',17)+",2,"+unit+",isotropic_omnidirectional,fixture-response\n").toUtf8();}
  const auto responsePath=out+"/response.csv";write(responsePath,csv);auto original=p.toJson();WorkbenchWidget host;host.setProject(p);host.resize(1000,760);host.show();app.processEvents();auto button=host.findChild<QPushButton*>("spaceradDeviceResponse");if(!button)return 1;button->click();app.processEvents();auto dialog=host.findChild<QDialog*>("spaceradDeviceResponseDialog");if(!dialog||!dialog->isVisible())return 1;
  dialog->findChild<QLineEdit*>("spaceradDeviceResponseInput")->setText(responsePath);dialog->findChild<QLineEdit*>("spaceradDeviceResponseOutput")->setText(out+"/study");dialog->findChild<QPushButton*>("spaceradDeviceResponseRun")->click();auto results=dialog->findChild<QTableWidget*>("spaceradDeviceResponseResults");if(results->rowCount()!=int(targets)||host.project().toJson()!=original)return 1;
  auto report=loadDeviceResponseStudy(out+"/study");if(report.at("rows").size()!=targets)return 1;std::cout<<"Native response table folded for "<<targets<<" sensitive placements.\n";dialog->close();host.showDeviceResponseStudy(out+"/study");app.processEvents();auto history=host.findChild<QDialog*>("spaceradDeviceResponseDialog");if(!history||history->findChild<QTableWidget*>("spaceradDeviceResponseResults")->rowCount()!=int(targets))return 1;history->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();
  auto saved=QFile(out+"/study/response.csv");if(!saved.open(QIODevice::ReadOnly))return 1;auto raw=saved.readAll();write(out+"/study/response.csv",raw+"\n");host.showDeviceResponseStudy(out+"/study");app.processEvents();auto tampered=host.findChild<QDialog*>("spaceradDeviceResponseDialog");if(!tampered||tampered->findChild<QTableWidget*>("spaceradDeviceResponseResults")->rowCount()!=0||tampered->findChild<QLabel*>("spaceradDeviceResponseNotice")->text().isEmpty())return 1;write(out+"/study/response.csv",raw);std::cout<<"Native offline review rejects tampered response archive.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
