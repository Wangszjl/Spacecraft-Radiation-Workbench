#include "Geometry.h"
#include "RadiationProject.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const std::string& label){++checks;if(!ok)throw std::runtime_error("FAIL: "+label);}
bool near(double a,double b,double eps=1e-8){return std::abs(a-b)<=eps;}
bool near3(const Vector3& a,const Vector3& b,double eps=1e-8){return near(a[0],b[0],eps)&&near(a[1],b[1],eps)&&near(a[2],b[2],eps);}
template<class F> void rejects(F&& f,const std::string& label){bool did=false;try{f();}catch(...){did=true;}check(did,label);}
Project base(){Project p=Project::demo();p.radius=100;p.volumes.clear();p.assemblies.clear();return p;}
Volume solid(std::string id,Vector3 c,Vector3 s,bool target=false,Vector3 r={}){
    Volume v{std::move(id),"G4_Al",c,s,0,target};v.rotation=r;return v;
}
Project pairProject(Vector3 b,Vector3 rotation={}){
    auto p=base();p.volumes={solid("A",{0,0,0},{4,1,1},true,{0,0,45}),solid("B",b,{4,1,1},false,rotation)};return p;
}
}
int main(){
    try{
        // Hierarchical active rotations and translations have independently calculated coordinates.
        auto p=base();p.assemblies={{"A","",{10,0,0},{0,0,90},true},{"B","A",{2,0,0},{90,0,0},true}};
        p.volumes={solid("target",{0,3,0},{1,1,1},true,{0,90,0})};p.volumes[0].parent="B";
        auto resolved=resolveGeometry(p);
        check(resolved.size()==1,"one resolved volume");
        check(near3(resolved[0].transform.translation,{10,2,3}),"nested world translation");
        check(near3(resolved[0].transform.apply({1,0,0}),{9,2,3}),"composed Rz*Ry*Rx world point");
        auto euler=base();euler.volumes={solid("target",{0,0,0},{1,1,1},true,{30,40,50})};
        auto basis=resolveGeometry(euler)[0].transform;
        check(near3(basis.apply({1,0,0}),{0.49240387650610407,0.5868240888334652,-0.6427876096865393}),"three-angle Euler X basis expected independently");
        check(near3(basis.apply({0,1,0}),{-0.456825992585671,0.8028723374794714,0.38302222155948895}),"three-angle Euler Y basis expected independently");
        check(near3(basis.apply({0,0,1}),{0.7408430568614905,0.10504046113295196,0.6634139481689384}),"three-angle Euler Z basis expected independently");

        // This pair's world AABBs overlap, while the oriented boxes are separated by their short axis.
        const double root2=std::sqrt(2.0);
        auto separated=pairProject({-root2,root2,0},{0,0,45});
        auto rr=resolveGeometry(separated);double amin[2]={1e9,1e9},amax[2]={-1e9,-1e9},bmin[2]={1e9,1e9},bmax[2]={-1e9,-1e9};
        for(int mask=0;mask<8;++mask){Vector3 q{};for(int k=0;k<3;++k)q[k]=((mask>>k)&1?1:-1)*separated.volumes[0].size[k]/2;
            auto a=rr[0].transform.apply(q),b=rr[1].transform.apply(q);
            for(int k=0;k<2;++k){amin[k]=std::min(amin[k],a[k]);amax[k]=std::max(amax[k],a[k]);bmin[k]=std::min(bmin[k],b[k]);bmax[k]=std::max(bmax[k],b[k]);}}
        check(amin[0]<bmax[0]&&bmin[0]<amax[0]&&amin[1]<bmax[1]&&bmin[1]<amax[1],"separated OBBs have overlapping AABBs");
        try{validateGeometry(separated);check(true,"SAT accepts separated rotated boxes");}catch(...){check(false,"SAT accepts separated rotated boxes");}
        auto intersecting=pairProject({1/root2,1/root2,0},{0,0,45});
        rejects([&]{validateGeometry(intersecting);},"SAT rejects rotated material intersection");

        auto touching=base();touching.volumes={solid("left",{0,0,0},{2,2,2},true),solid("right",{2,0,0},{2,2,2})};
        try{validateGeometry(touching);check(true,"exact face contact is allowed");}catch(...){check(false,"exact face contact is allowed");}
        auto cavity=base();cavity.assemblies={{"rotated_shell","",{4,5,6},{17,29,41},true}};
        auto shell=solid("shell",{0,0,0},{10,10,10});shell.wall=1;shell.parent="rotated_shell";
        auto cavityTarget=solid("cavity_target",{0,0,0},{2,2,2},true);cavityTarget.parent="rotated_shell";
        cavity.volumes={shell,cavityTarget};
        auto slabs=materialBoxes(shell);double materialVolume=0;for(const auto& b:slabs)materialVolume+=b.size[0]*b.size[1]*b.size[2];
        check(slabs.size()==6,"shell decomposes into six disjoint slabs");
        check(near(materialVolume,488),"shell slab volume excludes 8x8x8 cavity");
        try{validateGeometry(cavity);check(true,"rotated shell contains same-parent device in cavity");}catch(...){check(false,"rotated shell contains same-parent device in cavity");}
        auto wallPenetration=cavity;wallPenetration.volumes[1].center={3.5,0,0};
        rejects([&]{validateGeometry(wallPenetration);},"same-parent device penetrating rotated shell wall is rejected");

        auto cycle=base();cycle.assemblies={{"A","B",{},{},true},{"B","A",{},{},true}};cycle.volumes={solid("target",{0,0,0},{1,1,1},true)};
        rejects([&]{validateGeometry(cycle);},"assembly cycle rejected");
        auto missing=base();missing.assemblies={{"A","absent",{},{},true}};missing.volumes={solid("target",{0,0,0},{1,1,1},true)};
        rejects([&]{validateGeometry(missing);},"missing assembly parent rejected");
        auto dangling=base();auto dv=solid("target",{0,0,0},{1,1,1},true);dv.parent="absent";dangling.volumes={dv};
        rejects([&]{validateGeometry(dangling);},"dangling volume parent rejected");
        auto duplicate=base();duplicate.assemblies={{"same","",{},{},true}};duplicate.volumes={solid("same",{0,0,0},{1,1,1},true)};
        rejects([&]{validateGeometry(duplicate);},"cross-node duplicate identifier rejected");
        auto nonfinite=base();nonfinite.volumes={solid("target",{std::numeric_limits<double>::quiet_NaN(),0,0},{1,1,1},true)};
        rejects([&]{validateGeometry(nonfinite);},"nonfinite position rejected");
        auto nonfiniteRotation=base();nonfiniteRotation.volumes={solid("target",{0,0,0},{1,1,1},true,{0,std::numeric_limits<double>::infinity(),0})};
        rejects([&]{validateGeometry(nonfiniteRotation);},"nonfinite rotation rejected");
        auto outside=base();outside.radius=12;outside.volumes={solid("target",{0,9,0},{10,.5,.5},true,{0,0,90})};
        rejects([&]{validateGeometry(outside);},"rotated corner outside source sphere rejected");

        // Preview visibility is not part of placement or material-volume calculation.
        auto visible=base();visible.assemblies={{"frame","",{3,4,5},{10,20,30},true}};
        auto child=solid("target",{1,2,3},{4,5,6},true,{5,6,7});child.parent="frame";visible.volumes={child};
        Json before=geometrySnapshot(visible);visible.assemblies[0].visible=false;visible.volumes[0].visible=false;
        Json after=geometrySnapshot(visible);
        for(const char* key:{"world_center_mm","world_rotation","material_volume_mm3"})
            check(before["volumes"][0][key]==after["volumes"][0][key],std::string("visibility leaves ")+key+" unchanged");
        check(before["volumes"][0]["visible"]!=after["volumes"][0]["visible"],"snapshot reports changed preview visibility");

        // Existing v2 geometry keeps its legacy six-field representation; v3 is explicit and round-trips.
        Json v2=Project::demo().toJson();
        check(v2["schema"]=="scdat.radiation/2","default project remains schema v2");
        check(v2["volumes"][0].size()==6,"v2 volume has only legacy geometry fields");
        Project v2round=Project::fromJson(v2);
        check(v2round.toJson()["volumes"]==v2["volumes"],"v2 geometry round-trip is unchanged");
        auto v3p=visible;v3p.assemblies[0].visible=true;v3p.volumes[0].visible=false;
        Json v3=v3p.toJson();
        check(v3["schema"]=="scdat.radiation/3"&&v3["geometry_contract"]=="scdat.box-assembly/1","assembly geometry emits v3 contract");
        check(Project::fromJson(v3).toJson()["volumes"]==v3["volumes"],"v3 transform and visibility round-trip");
        Json mislabeled2=v3;mislabeled2["schema"]="scdat.radiation/2";
        rejects([&]{Project::fromJson(mislabeled2);},"v2 rejects v3 assembly fields");
        Json mislabeled1=v3;mislabeled1["schema"]="scdat.radiation/1";
        rejects([&]{Project::fromJson(mislabeled1);},"v1 rejects v3 assembly fields");
        auto unknownShape=v3;unknownShape["volumes"][0]["shape"]="sphere";
        rejects([&]{Project::fromJson(unknownShape);},"unknown shapes must not silently become boxes");
        auto deep=base();deep.volumes={solid("target",{0,0,0},{1,1,1},true)};
        for(int i=0;i<128;++i)deep.assemblies.push_back({"A"+std::to_string(i),i?"A"+std::to_string(i-1):"",{},{},true});
        validateGeometry(deep);check(true,"128 ordered assembly levels allowed");
        deep.assemblies.push_back({"too_deep","A127",{},{},true});
        rejects([&]{validateGeometry(deep);},"129 ordered assembly levels rejected despite cached parents");
        std::cout<<"geometry contract checks: "<<checks<<" passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
