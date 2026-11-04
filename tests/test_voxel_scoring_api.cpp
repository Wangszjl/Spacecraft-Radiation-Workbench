#include <spacerad/VoxelScoring.h>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
namespace {int checks=0;void check(bool pass,const char* name){++checks;if(!pass)throw std::runtime_error(name);}template<class F>void rejects(F f,const char* name){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,name);}}
int main(){try{
 auto base=Project::demo();const auto original=base.toJson();
 auto configured=configureVoxelScoring(base,"OBC_Si",{2,2,1},{0,.1,1,10,100},20,.3);
 check(base.toJson()==original,"voxel configuration is immutable");
 check(configured.voxelScoring.at("schema")=="scdat.voxel-scoring/1"&&configured.toJson().at("schema")=="scdat.radiation/7","voxel scoring selects v7 project schema");
 check(Project::fromJson(configured.toJson()).voxelScoring==configured.voxelScoring,"voxel settings round trip");
 check(disableVoxelScoring(configured).voxelScoring.is_null()&&!configured.voxelScoring.is_null(),"disable returns a separate project");
 rejects([&]{(void)configureVoxelScoring(base,"missing",{2,1,1},{0,1});},"unknown volume rejected");
 rejects([&]{(void)configureVoxelScoring(base,"Spacecraft_Al",{2,1,1},{0,1});},"non-sensitive shell rejected");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{33,1,1},{0,1});},"axis above 32 rejected");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{16,16,17},{0,1});},"grid above 4096 cells rejected");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{2,1,1},{1,2});},"energy histogram must start at zero");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{2,1,1},{0,2,1});},"non-increasing energy edges rejected");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{2,1,1},{0,1},1,.2);},"minimum hit threshold enforced");
 rejects([&]{(void)configureVoxelScoring(base,"OBC_Si",{2,1,1},{0,1},20,1.1);},"maximum RSE threshold enforced");
 auto curved=base;for(auto& v:curved.volumes)if(v.id=="OBC_Si")v.shape="sphere";rejects([&]{(void)configureVoxelScoring(curved,"OBC_Si",{2,1,1},{0,1});},"curved sensitive region rejected");
 std::cout<<checks<<" voxel scoring API checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
