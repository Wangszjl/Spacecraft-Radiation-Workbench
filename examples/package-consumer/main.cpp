#include <QCoreApplication>
#ifdef TEST_LEGACY_HEADER
#include "RadiationProject.h"
#endif
#include <spacerad/AnalysisService.h>
#include <spacerad/Authoring.h>
#include <type_traits>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);
#ifdef TEST_LEGACY_HEADER
 static_assert(!std::is_same_v<scdat::radiation::Project,spacerad::Project>);
#endif
 auto p=spacerad::Project::assemblyDemo();p.validate();auto part=spacerad::suggestPart(p,"box",true);auto authored=spacerad::addPart(p,part);if(authored.volumes.size()!=p.volumes.size()+1)return 3;spacerad::AnalysisService service;std::cout<<p.name<<"; service="<<service.state().toStdString()<<'\n';return service.running()?1:0;}
