#include <spacerad/ShieldingScan.h>
#include <QCoreApplication>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=2) {std::cerr<<"Pass a saved scan directory\n";return 2;}
    try {
        const auto report=spacerad::loadShieldingScan(app.arguments().at(1));
        const auto plan=spacerad::planShieldingScan(spacerad::Project::demo(),{"Spacecraft_Al",{0,1}});
        spacerad::ShieldingScanService service;
        if(spacerad::shieldingScanCsv(report).isEmpty() || plan.at("cases").size()!=2 || service.running())return 3;
        std::cout<<"installed-core-scan: status="<<report.at("status")<<"; curves="<<report.at("curves").size()<<"; state="<<service.state().toStdString()<<'\n';
        return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
