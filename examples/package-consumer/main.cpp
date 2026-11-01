#include <QCoreApplication>
#include <QFileInfo>
#ifdef TEST_LEGACY_HEADER
#include "RadiationProject.h"
#endif
#include <spacerad/AnalysisService.h>
#include <spacerad/Authoring.h>
#include <spacerad/Environment.h>
#include <spacerad/Results.h>
#include <type_traits>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);
#ifdef TEST_LEGACY_HEADER
 static_assert(!std::is_same_v<scdat::radiation::Project,spacerad::Project>);
#endif
 auto p=spacerad::Project::assemblyDemo();p.validate();auto part=spacerad::suggestPart(p,"box",true);auto authored=spacerad::addPart(p,part);if(authored.volumes.size()!=p.volumes.size()+1)return 3;auto environment=spacerad::setManualSpectrum(p,p.spectra.size(),"e-",{1,2,3},{4,2,0});if(environment.spectra.back().integral!=4)return 4;spacerad::AnalysisService service;std::cout<<p.name<<"; manual-integral="<<environment.spectra.back().integral<<"; service="<<service.state().toStdString();if(argc>=2){auto runtime=spacerad::loadRuntimeFile(app.arguments().at(1));if(!QFileInfo(runtime.worker).isAbsolute()||!QFileInfo(runtime.dataRoot).isAbsolute())return 5;std::cout<<"; runtime-loader=core";}if(argc==4){auto a=spacerad::loadArchivedRun(app.arguments().at(2)),b=spacerad::loadArchivedRun(app.arguments().at(3));auto comparison=spacerad::compareRuns(a,b);if(comparison.at("rows").empty()||spacerad::comparisonCsv(comparison).isEmpty())return 6;std::cout<<"; offline-comparison="<<comparison.at("rows").size();}std::cout<<'\n';return service.running()?1:0;}
