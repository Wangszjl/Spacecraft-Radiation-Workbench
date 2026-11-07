#include <spacerad/PartitionStudy.h>
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;try{
    auto p=spacerad::Project::demo();auto config=spacerad::defaultPartitionStudySettings(p,50);
    auto plan=spacerad::planPartitionStudy(p,{"Spacecraft_Al",{0,1}},config);
    auto report=spacerad::loadPartitionStudy(app.arguments()[1]);spacerad::PartitionStudyService service;
    if(plan.at("jobs").empty()||spacerad::partitionStudyCsv(report).isEmpty()||service.running())return 3;
    std::cout<<"installed-native-partition: "<<report.at("status")<<"; rows="<<report.at("rows").size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
