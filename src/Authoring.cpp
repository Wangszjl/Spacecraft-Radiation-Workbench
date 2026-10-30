#include "spacerad/Authoring.h"
#include "Geometry.h"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace spacerad {
namespace {
size_t authoredIndex(const Project& project,const std::string& id){
    auto found=std::find_if(project.volumes.begin(),project.volumes.end(),[&](const Volume& v){return v.id==id;});
    if(found==project.volumes.end())throw std::runtime_error("Select an authored volume; template descendants are not edited implicitly");
    return size_t(found-project.volumes.begin());
}
}
Project addPart(const Project& project,const Volume& part){
    project.validate();auto result=project;result.volumes.push_back(part);result.validate();return result;
}
Project replacePart(const Project& project,const std::string& id,const Volume& part){
    project.validate();auto result=project;result.volumes.at(authoredIndex(project,id))=part;result.validate();return result;
}
Project removePart(const Project& project,const std::string& id){
    project.validate();auto result=project;auto index=authoredIndex(project,id);result.volumes.erase(result.volumes.begin()+index);result.validate();return result;
}
Volume suggestPart(const Project& project,const std::string& shape,bool sensitive){
    project.validate();if(shape!="box"&&shape!="cylinder"&&shape!="sphere")throw std::runtime_error("Supported authored shapes: box, cylinder, sphere");
    std::set<std::string> ids;const auto geometry=engine::compileGeometry(project);
    for(const auto& v:geometry.volumes)ids.insert(v.id);
    for(const auto& a:geometry.assemblies)ids.insert(a.id);
    for(const auto& i:project.instances)ids.insert(i.id);
    Volume part{};part.shape=shape;part.sensitive=sensitive;part.material=sensitive?"G4_Si":"G4_Al";part.size=shape=="box"?std::array<double,3>{20,20,sensitive?1.:20.}:shape=="cylinder"?std::array<double,3>{20,20,30}:std::array<double,3>{20,20,20};
    for(size_t i=1;i<=ids.size()+1;++i){std::ostringstream name;name<<(sensitive?"Device_":"Body_")<<std::setw(3)<<std::setfill('0')<<i;if(!ids.count(name.str())){part.id=name.str();break;}}
    for(double fraction:{0.,.25,.5,.7,.85})for(int x:{0,1,-1})for(int y:{0,1,-1})for(int z:{0,1,-1}){
        if((fraction==0&&(x!=0||y!=0||z!=0))||(fraction>0&&x==0&&y==0&&z==0))continue;
        part.center={x*project.radius*fraction,y*project.radius*fraction,z*project.radius*fraction};
        try{(void)addPart(project,part);return part;}catch(const std::exception&){}
    }
    throw std::runtime_error("No valid suggested placement for a 20 mm component; enlarge the source sphere or edit the existing model");
}
}
