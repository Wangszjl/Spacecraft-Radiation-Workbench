#include <spacerad/WorkbenchWidget.h>
#include <spacerad/VoxelScoring.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
namespace {Json checks=Json::array();void check(bool value,const char* name){checks.push_back({{"name",name},{"passed",value}});if(!value)throw std::runtime_error(name);} QPushButton* button(QWidget& widget,const char* name){return widget.findChild<QPushButton*>(name);} }
int main(int argc,char** argv){
 QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);
 if(argc!=4)return 2;const QString runtime=QString::fromLocal8Bit(argv[1]),projectPath=QString::fromLocal8Bit(argv[2]),out=QString::fromLocal8Bit(argv[3]);
 if(QFileInfo::exists(out)||!QDir().mkpath(out))return 2;bool passed=false;
 try{
  QFile source(projectPath);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open test voxel project");auto project=Project::fromJson(Json::parse(source.readAll().toStdString()));project.events=4000;
  WorkbenchWidget widget;widget.setRuntime(loadRuntimeFile(runtime));widget.setProject(project);widget.resize(1150,850);widget.show();app.processEvents();
  auto* enabled=widget.findChild<QCheckBox*>("spaceradVoxelEnabled");check(enabled&&enabled->isChecked(),"v7 project opens with voxel scoring enabled");
  check(widget.findChild<QComboBox*>("spaceradVoxelVolume")->currentData().toString()=="OBC_Si"&&widget.project().voxelScoring==project.voxelScoring,"grid and sensitive target load unchanged");
  widget.setProject(disableVoxelScoring(project));check(!enabled->isChecked(),"ordinary analytic project opens with voxel scorer off");enabled->setChecked(true);widget.findChild<QSpinBox*>("spaceradVoxelMinHits")->setValue(20);widget.findChild<QDoubleSpinBox*>("spaceradVoxelMaxRse")->setValue(.5);button(widget,"spaceradApplyVoxel")->click();check(widget.project().voxelScoring==project.voxelScoring,"configuration controls apply exact v7 scorer into the project");
  widget.setProject(disableVoxelScoring(project));check(!enabled->isChecked(),"legacy geometry opens without voxel scorer");enabled->setChecked(true);auto* selectedVolume=widget.findChild<QComboBox*>("spaceradVoxelVolume");selectedVolume->setCurrentIndex(selectedVolume->findData("OBC_Si"));widget.findChild<QSpinBox*>("spaceradVoxelCount0")->setValue(2);widget.findChild<QSpinBox*>("spaceradVoxelCount1")->setValue(2);widget.findChild<QSpinBox*>("spaceradVoxelCount2")->setValue(2);widget.findChild<QLineEdit*>("spaceradVoxelEdepEdges")->setText("0, .1, 1, 10, 1000");widget.findChild<QPushButton*>("spaceradApplyVoxel")->click();project=widget.project();check(project.voxelScoring.at("counts")==Json::array({2,2,2})&&project.toJson().at("schema")=="scdat.radiation/7","voxel editor commits selected target and grid to schema v7");
  const auto saved=out+"/roundtrip.json";widget.saveProject(saved);QFile roundtrip(saved);roundtrip.open(QIODevice::ReadOnly);check(Project::fromJson(Json::parse(roundtrip.readAll().toStdString())).voxelScoring==project.voxelScoring,"v7 voxel settings persist through desktop save");
  QEventLoop loop;QString failure;int resultSignals=0;QObject::connect(&widget,&WorkbenchWidget::resultReady,&loop,[&](const QByteArray&){++resultSignals;loop.quit();});QObject::connect(widget.service(),&AnalysisService::failed,&loop,[&](const QString& e){failure=e;loop.quit();});QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{failure="voxel transport timeout";widget.cancel();loop.quit();});timeout.start(60000);
  const auto before=widget.project().toJson();widget.runTo(out+"/job");loop.exec();if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
  const auto accepted=Json::parse(widget.acceptedResult().toStdString());check(resultSignals==1&&accepted.contains("voxel_field")&&accepted.at("voxel_field").at("rows").size()==8,"real Geant4 run returns all 2x2x2 voxel cells");
  check(widget.project().toJson()==before&&widget.findChild<QPushButton*>("spaceradVoxelView")->isEnabled(),"accepted voxel scoring preserves the project and enables field view");
  const auto& field=accepted.at("voxel_field");double weightedCellsDose=0;for(const auto& row:field.at("rows"))weightedCellsDose+=row.at("tid_Gy").get<double>()*row.at("mass_kg").get<double>();weightedCellsDose/=field.at("target_mass_kg").get<double>();double targetDose=0;for(const auto& target:accepted.at("targets"))if(target.at("id")=="OBC_Si")targetDose=target.at("tid_Gy").get<double>();
  check(std::abs(weightedCellsDose-targetDose)<=1e-10*std::max(1e-30,std::abs(targetDose)),"mass-weighted cell TID matches whole-target TID; raw cells are not summed as doses");
  widget.findChild<QPushButton*>("spaceradVoxelView")->click();app.processEvents();auto* dialog=widget.findChild<QDialog*>("spaceradVoxelDialog");check(dialog&&dialog->isVisible(),"accepted field opens spatial slice viewer");
  check(dialog->findChild<QTableWidget*>("spaceradVoxelTable")->rowCount()==4,"Z slice table lists the four cells in plane");
  dialog->findChild<QComboBox*>("spaceradVoxelAxis")->setCurrentIndex(0);check(dialog->findChild<QTableWidget*>("spaceradVoxelTable")->rowCount()==4,"slice orientation changes and preserves plane cell count");
  check(dialog->grab().save(out+"/slice.png")&&widget.findChild<QWidget*>("spaceradGeometryView")->grab().save(out+"/overlay.png"),"slice and VTK 3D voxel overlay render");
  bool pickerShown=false;QTimer::singleShot(80,dialog,[&]{if(auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){pickerShown=true;picker->setDirectory(out);QTimer::singleShot(100,picker,[picker]{QMetaObject::invokeMethod(picker,"accept");});}});
  QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,dialog,[]{if(auto* modal=QApplication::activeModalWidget())modal->close();});watchdog.start(5000);dialog->findChild<QPushButton*>("spaceradVoxelExport")->click();
  QDir parent(out);const auto exports=parent.entryList(QStringList{"voxel-field-*"},QDir::Filters(QDir::Dirs|QDir::NoDotAndDotDot),QDir::Name);
  check(pickerShown&&!exports.isEmpty(),"voxel output directory picker publishes an export");const auto bundle=out+"/"+exports.last();check(QFileInfo(bundle+"/field.json").isFile()&&QFileInfo(bundle+"/field.vti").isFile()&&QFileInfo(bundle+"/field.csv").isFile()&&QFileInfo(bundle+"/manifest.json").isFile(),"atomic voxel export includes JSON, VTI, CSV and SHA manifest");
  passed=true;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
 QFile summary(out+"/summary.json");summary.open(QIODevice::WriteOnly);summary.write(Json{{"passed",passed},{"checks",checks}}.dump(2).c_str());std::cout<<checks.size()<<" native voxel checks; passed="<<passed<<'\n';return passed?0:1;
}
