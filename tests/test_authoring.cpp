#include <spacerad/Authoring.h>
#include <iostream>
#include <stdexcept>
using namespace spacerad;
int main(){int checks=0;auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};auto rejects=[&](auto operation){try{operation();return false;}catch(const std::exception&){return true;}};try{
 auto p=Project::assemblyDemo();auto original=p.toJson();
 for(auto shape:{"box","cylinder","sphere"}){
  auto part=suggestPart(p,shape,true);auto q=addPart(p,part);check(q.volumes.size()==p.volumes.size()+1,"new part inserted");check(part.sensitive&&part.material=="G4_Si"&&part.shape==shape,"suggested material and shape");check(p.toJson()==original,"input project immutable");check(removePart(q,part.id).toJson()==original,"removal restores exact project");
 }
 auto a=suggestPart(p,"box",false);auto q=addPart(p,a);auto b=suggestPart(q,"box",false);check(a.id!=b.id,"suggestions have globally unique IDs");
 auto duplicate=b;duplicate.id=a.id;check(rejects([&]{addPart(q,duplicate);}),"duplicate ID rejected");
 auto outside=b;outside.center={p.radius*2,0,0};check(rejects([&]{addPart(p,outside);}),"outside-source placement rejected");
 auto overlapping=a;overlapping.id="Overlapping_Copy";check(rejects([&]{addPart(q,overlapping);}),"overlap rejected");
 auto renamed=a;renamed.id="Renamed_Body";renamed.material="G4_Cu";auto renamedProject=replacePart(q,a.id,renamed);check(renamedProject.volumes.back().id==renamed.id&&renamedProject.volumes.back().material=="G4_Cu","explicit replacement updates identity/material");
 check(q.volumes.back().id==a.id,"replacement does not modify source");
 auto sole=Project::demo();std::string target;for(auto& v:sole.volumes){if(v.sensitive&&target.empty())target=v.id;else v.sensitive=false;}sole.validate();check(rejects([&]{removePart(sole,target);}),"last sensitive target cannot be removed");
 check(rejects([&]{removePart(p,"not-an-authored-volume");}),"unknown/implicit component not guessed");
 check(rejects([&]{suggestPart(p,"mesh",false);}),"unsupported shape rejected");
 std::cout<<checks<<" authoring checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
