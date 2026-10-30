#include "spacerad/AnalysisService.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 AnalysisService service;QTemporaryDir temporary;auto dir=temporary.filePath("must-not-create");bool rejected=false;try{service.start(Project::demo(),dir);}catch(const std::exception&){rejected=true;}
 if(!rejected||QDir(dir).exists())throw std::runtime_error("Missing runtime must reject before creating output");
 std::cout<<"service rejection contract passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
