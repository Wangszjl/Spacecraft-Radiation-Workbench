#include <spacerad/ImportanceConfig.h>
#include <spacerad/VoxelScoring.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace spacerad;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void reject(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid sampling settings accepted");}
}
int main()try{
    auto p=Project::demo();
    Spectrum electron;electron.particle="e-";electron.energy={.1,1,3};electron.values={10,5,1};electron.prepare();p.spectra.push_back(electron);p.validate();
    const auto original=p.toJson();
    const double physical=.20000000000000004,tail=.0010000000000000011;
    auto configured=configureImportanceSampling(p,physical,tail,{{"e-",.3},{"proton",.7}});
    check(p.toJson()==original,"Configuring sampling changed the caller input");
    check(configured.sourceSampling.at("physical_fraction")==physical&&configured.sourceSampling.at("tail_fraction")==tail,
          "Sampling values were rounded");
    check(configured.toJson().at("schema")=="scdat.radiation/8"&&Project::fromJson(configured.toJson()).sourceSampling==configured.sourceSampling,
          "Weighted configuration did not roundtrip");
    check(configured.expectedPrimaries()==p.expectedPrimaries()&&configured.duration==p.duration&&configured.seed==p.seed,
          "Changing proposal changed physical exposure or seed");
    check(disableImportanceSampling(configured).toJson()==original,"Disabling sampling changed physical input");
    reject([&]{configureImportanceSampling(p,0,tail);});
    reject([&]{configureImportanceSampling(p,.2,std::numeric_limits<double>::quiet_NaN());});
    reject([&]{configureImportanceSampling(p,.2,tail,{{"proton",1.0}});});
    reject([&]{configureImportanceSampling(p,.2,tail,{{"e-",.1},{"proton",.8}});});
    auto voxels=configureVoxelScoring(p,"OBC_Si",{2,1,1},{0,1,10});
    const auto before=voxels.toJson();
    reject([&]{configureImportanceSampling(voxels,.2,tail);});
    check(voxels.toJson()==before,"Rejected configuration changed a voxel project");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
