#include "Geometry.h"
#include <iostream>
#include <limits>
#include <cmath>
#include <stdexcept>
using namespace spacerad::engine;
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
bool has(const Json& report,const std::string& code,const std::string& id=""){
    for(const auto& issue:report.at("issues"))if(issue.at("code")==code){if(id.empty())return true;for(const auto& related:issue.at("related_ids"))if(related==id)return true;}return false;
}
bool rejects(const Project& p){try{validateGeometry(p);return false;}catch(const std::exception&){return true;}}
Project pair(){auto p=Project::demo();p.radius=100;p.volumes={Volume{"A","G4_Si",{0,0,0},{10,10,10},0,true},Volume{"B","G4_Al",{20,0,0},{10,10,10},0,false}};return p;}
int main(){try{
    auto p=pair();auto good=geometryDiagnostics(p);check(good.at("status")=="valid","valid pair diagnosed");check(good.at("complete")==true,"complete good scan");check(!rejects(p),"validator agrees valid");
    auto bad=p;bad.volumes[1].center={0,0,0};bad.volumes[1].material="";bad.radius=5;auto report=geometryDiagnostics(bad);
    check(report.at("status")=="invalid","multiple faults invalid");check(has(report,"material_identifier","B"),"missing material ID");check(has(report,"source_clearance","A")&&has(report,"source_clearance","B"),"both clearance faults");check(has(report,"material_overlap_or_unresolved_contact","A")&&has(report,"material_overlap_or_unresolved_contact","B"),"pair IDs retained");check(rejects(bad),"validator rejects bad geometry");
    auto shape=p;shape.volumes[1].size[0]=-1;auto invalidShape=geometryDiagnostics(shape);check(has(invalidShape,"shape_invalid","B"),"invalid dimensions identified");check(invalidShape.at("skipped_pair_count")==1,"bad solid excluded from pair scan");check(rejects(shape),"bad dimensions reject transport");
    auto empty=p;empty.volumes.clear();check(has(geometryDiagnostics(empty),"geometry_empty"),"empty geometry");auto noTarget=p;noTarget.volumes[0].sensitive=false;check(has(geometryDiagnostics(noTarget),"sensitive_volume_missing"),"missing sensitive volume");
    auto topology=p;topology.volumes[0].parent="missing";auto unresolved=geometryDiagnostics(topology);check(has(unresolved,"topology_unresolved"),"unresolved topology reported");check(unresolved.at("complete")==false&&unresolved.at("topology_resolved")==false,"unresolved never complete");
    auto hidden=bad;hidden.radius=100;hidden.volumes[1].visible=false;check(has(geometryDiagnostics(hidden),"material_overlap_or_unresolved_contact","B"),"hidden geometry still checked");
    auto contact=p;contact.volumes[1].center={10,0,0};check(geometryDiagnostics(contact).at("status")=="valid"&&!rejects(contact),"touching boxes accepted consistently");
    auto shell=p;shell.volumes[0].sensitive=false;shell.volumes[0].size={40,40,40};shell.volumes[0].wall=2;shell.volumes[1].center={0,0,0};shell.volumes[1].sensitive=true;check(geometryDiagnostics(shell).at("status")=="valid"&&!rejects(shell),"empty box cavity preserved");
    auto instance=Project::instanceDemo();auto instanceGood=geometryDiagnostics(instance);check(instanceGood.at("status")=="valid","rotated instances valid");instance.instances[1].center=instance.instances[0].center;instance.instances[1].rotation=instance.instances[0].rotation;check(has(geometryDiagnostics(instance),"material_overlap_or_unresolved_contact","Unit_A/Chip"),"expanded stable instance IDs");
    auto curves=Project::shapeDemo();check(geometryDiagnostics(curves).at("status")=="valid","curved model diagnostics");
    auto limited=geometryDiagnostics(p,256,0);check(limited.at("status")=="incomplete"&&limited.at("complete")==false,"zero pair budget cannot pass");check(limited.at("pair_limit_reached")==true,"pair limit explicit");
    auto truncated=geometryDiagnostics(bad,1);check(truncated.at("status")=="invalid"&&truncated.at("issue_limit_reached")==true&&truncated.at("complete")==false,"issue limit explicit");check(truncated.at("issues").size()==1,"issue limit enforced");
    bool zero=false;try{geometryDiagnostics(p,0);}catch(const std::exception&){zero=true;}check(zero,"zero issue limit rejected");
    auto finite=p;finite.volumes[0].center[0]=std::numeric_limits<double>::infinity();check(geometryDiagnostics(finite).at("status")=="invalid","nonfinite transform cannot pass");
    for(int k=0;k<32;++k){auto varied=pair();varied.volumes[1].center={double(k%8)*2,0,0};varied.volumes[0].rotation={0,0,double(k)*7};if(k%3==0)varied.volumes[1].shape="sphere";else if(k%3==1)varied.volumes[1].shape="cylinder";check((geometryDiagnostics(varied).at("status")=="valid")==!rejects(varied),"bounded diagnostics agrees with full validator on varied analytic pairs");}
    auto single=p;single.volumes.resize(1);single.volumes[0].size[0]=-1;check(geometryDiagnostics(single).at("complete")==false,"single invalid shape has incomplete dependent checks");
    std::cout<<checks<<" diagnostics checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
