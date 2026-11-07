#include <spacerad/PartitionStudy.h>
#include <spacerad/OrbitEnvironment.h>
#include <spacerad/DeviceResponse.h>
#include <QCoreApplication>
#include <QFile>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=6)return 2;try{
    auto report=spacerad::loadPartitionStudy(app.arguments()[1]);spacerad::PartitionStudyService service;
    if(spacerad::partitionStudyCsv(report).isEmpty()||service.running())return 3;
    auto model=spacerad::loadOrbitModelJob(app.arguments()[2]);spacerad::OrbitEnvironmentService orbit;
    auto ddd=spacerad::loadDeviceResponseCsv(app.arguments()[3]);auto see=spacerad::loadDeviceResponseCsv(app.arguments()[4]);
    QFile projectFile(app.arguments()[5]);if(!projectFile.open(QIODevice::ReadOnly))return 5;
    auto responseProject=spacerad::Project::fromJson(spacerad::Json::parse(projectFile.readAll().toStdString()));
    auto dddResult=spacerad::foldDeviceResponseTable(responseProject,ddd);auto seeResult=spacerad::foldDeviceResponseTable(responseProject,see);
    if(orbit.running()||dddResult.at("rows").empty()||seeResult.at("rows").empty())return 4;
    std::cout<<"installed-SpaceRad: partition="<<report.at("status")<<"/"<<report.at("rows").size()<<"; orbit="<<model.at("samples")<<"; DDD/SEE="<<dddResult.at("rows").size()<<'/'<<seeResult.at("rows").size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
