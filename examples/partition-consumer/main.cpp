#include <spacerad/PartitionStudy.h>
#include <spacerad/OrbitEnvironment.h>
#include <spacerad/DeviceResponse.h>
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=4)return 2;try{
    auto p=spacerad::Project::demo();auto config=spacerad::defaultPartitionStudySettings(p,50);
    auto plan=spacerad::planPartitionStudy(p,{"Spacecraft_Al",{0,1}},config);
    auto report=spacerad::loadPartitionStudy(app.arguments()[1]);spacerad::PartitionStudyService service;
    if(plan.at("jobs").empty()||spacerad::partitionStudyCsv(report).isEmpty()||service.running())return 3;
    auto model=spacerad::loadOrbitModelJob(app.arguments()[2]);spacerad::OrbitEnvironmentService orbit;
    auto response=spacerad::loadDeviceResponseCsv(app.arguments()[3]);response.validate();if(orbit.running()||response.curves.empty())return 4;
    std::cout<<"installed-SpaceRad: partition="<<report.at("status")<<"/"<<report.at("rows").size()<<"; orbit="<<model.at("samples")<<"; response="<<response.metric<<'/'<<response.curves.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
