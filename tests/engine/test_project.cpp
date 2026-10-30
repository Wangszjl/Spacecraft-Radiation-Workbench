#include "RadiationProject.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace spacerad::engine;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected,"Invalid input accepted"); }
int main() {
    try {
        auto p=Project::demo();
        check(Project::fromJson(p.toJson()).toJson()==p.toJson(),"Project roundtrip");
        Spectrum flat; flat.particle="proton"; flat.energy={1,3}; flat.values={2,2}; flat.prepare();
        check(std::abs(flat.integral-4)<1e-12 && std::abs(flat.sample(.25)-1.5)<1e-12,"Flat spectrum inverse CDF");
        Spectrum ramp=flat; ramp.values={0,4}; ramp.prepare();
        check(std::abs(ramp.sample(.25)-2)<1e-12,"Rising linear inverse CDF");
        ramp.values={4,0}; ramp.prepare(); check(std::abs(ramp.sample(.75)-2)<1e-12,"Falling inverse CDF");
        p.spectra={flat}; p.radius=1000; p.duration=10;
        check(std::abs(p.expectedPrimaries()-pi*10000*4*10)<1e-6,"Sphere fluence normalization");
        Moments m; m.add(0);m.add(2); check(m.mean==1 && std::abs(m.standardError()-1)<1e-12,"Event statistics must include zero histories");
        rejects([&]{auto q=p;q.volumes[2].center={110,0,0};q.validate();}); // intersects electronics shell
        rejects([&]{auto q=p;q.volumes[3]=q.volumes[2];q.volumes[3].id="duplicate_position";q.validate();});
        rejects([&]{auto q=p;q.radius=100;q.validate();});
        rejects([&]{auto q=p.toJson();q["environment"]["flux_convention"]="integral";Project::fromJson(q);});
        rejects([&]{auto q=flat;q.energy={3,1};q.prepare();});
        rejects([&]{auto q=flat;q.values={0,0};q.prepare();});
        rejects([&]{auto q=p;q.events=1;q.validate();});
        rejects([&]{auto q=p.toJson();q["events"]=2.9;Project::fromJson(q);});
        auto csv=std::filesystem::temp_directory_path()/
            ("scdat-spectrum-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".csv");
        {std::ofstream file(csv);file<<"\xEF\xBB\xBF" "energy_MeV,differential_flux\n# verification\n1,2\n3,2\n";}
        auto omni=readSpectrumCsv(csv.u8string(),"proton",false);
        auto directional=readSpectrumCsv(csv.u8string(),"proton",true);
        std::filesystem::remove(csv);
        check(std::abs(omni.integral-4)<1e-12 && std::abs(directional.integral/omni.integral-4*pi)<1e-12,"CSV BOM/comment and per-sr conversion");
        std::cout<<"Radiation project: roundtrip, inverse CDF, sphere normalization, zero-history statistics and invalid-input checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
