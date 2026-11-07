#include <spacerad/PartitionStudy.h>
#include <spacerad/OrbitEnvironment.h>
#include <spacerad/DeviceResponse.h>
#include <QCoreApplication>
#include <QFile>
#include <iostream>
using namespace spacerad;
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=6)return 2;try{QFile input(app.arguments()[5]);if(!input.open(QIODevice::ReadOnly))return 5;auto project=Project::fromJson(Json::parse(input.readAll().toStdString()));auto partition=defaultPartitionStudySettings(project,50);auto plan=planPartitionStudy(project,{"Bus_Al",{.8,2.0}},partition);auto orbit=loadOrbitModelJob(app.arguments()[2]);OrbitEnvironmentService modelService;auto ddd=loadDeviceResponseCsv(app.arguments()[3]);auto see=loadDeviceResponseCsv(app.arguments()[4]);auto dddFold=foldDeviceResponseTable(project,ddd);auto seeFold=foldDeviceResponseTable(project,see);PartitionStudyService studyService;if(plan.at("jobs").empty()||modelService.running()||studyService.running()||dddFold.at("rows").empty()||seeFold.at("rows").empty())return 3;std::cout<<"installed-SpaceRad SDK: partition-jobs="<<plan.at("jobs").size()<<"; ephemeris-samples="<<orbit.at("samples")<<"; DDD-devices="<<dddFold.at("rows").size()<<"; SEE-devices="<<seeFold.at("rows").size()<<'\\n';return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\\n';return 1;}}
