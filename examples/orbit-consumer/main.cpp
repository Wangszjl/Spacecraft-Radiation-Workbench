#include <spacerad/OrbitEnvironment.h>
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;try{auto review=spacerad::loadOrbitModelJob(app.arguments()[1]);spacerad::OrbitEnvironmentService service;auto s=spacerad::OrbitEnvironmentSettings{};s.ephemeris="external.csv";if(service.running()||spacerad::OrbitEnvironmentSettings::fromJson(s.toJson()).frame!="GEO")return 3;std::cout<<"installed-orbit: "<<review.at("samples")<<" samples; "<<review.at("duration_s")<<" s; offline verified\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
