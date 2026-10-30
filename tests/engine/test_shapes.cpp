#include "Geometry.h"
#include "Instances.h"
#include "RadiationProject.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const std::string& label){++checks;if(!ok)throw std::runtime_error("FAIL: "+label);}
bool near(double a,double b,double tol=1e-9){return std::abs(a-b)<=tol;}
template<class F> void rejects(F&& f,const std::string& label){bool failed=false;try{f();}catch(...){failed=true;}check(failed,label);}
Project emptyProject(double radius=1000){auto p=Project::demo();p.radius=radius;p.volumes.clear();p.assemblies.clear();p.templates.clear();p.instances.clear();return p;}
Volume shape(std::string id,std::string kind,Vector3 center,Vector3 size,bool target=false,Vector3 rotation={}){
    Volume v{std::move(id),"G4_Si",center,size,0,target,"",rotation,true,std::move(kind)};return v;
}
Project two(Volume a,Volume b){auto p=emptyProject();p.volumes={std::move(a),std::move(b)};return p;}
Json siliconSnapshot(){return {{"schema","scdat.nist-materials/1"},{"engine_version","11.4.3"},
    {"provider","Geant4 G4NistManager"},{"isotopes_requested",false},{"materials",Json::array({
        {{"id","G4_Si"},{"density_kg_m3",2330.0},{"state","solid"},{"temperature_K",293.15},{"pressure_Pa",101325.0},
         {"elements",Json::array({{{"name","Silicon"},{"symbol","Si"},{"Z",14},{"A_g_mol",28.0855},{"mass_fraction",1.0}}})}}})}};}
Json legacyV1(){
    Json j=Project::demo().toJson();j["schema"]="scdat.radiation/1";
    auto& env=j["environment"];env.erase("quantity");env.erase("unit");env.erase("angular_distribution");env.erase("angular_convention");env.erase("metadata");
    env["flux_convention"]="omnidirectional_differential_cm-2_s-1_MeV-1";
    for(auto& s:env["spectra"]){s["differential_flux"]=s["values"];s.erase("values");s.erase("provenance");s.erase("interpolation");}
    return j;
}
}
int main(){
    try{
        // Analytic volume and surface references in mm and mm^2.
        auto sphere=shape("sphere","sphere",{0,0,0},{2,2,2},true);
        auto cylinder=shape("cylinder","cylinder",{0,0,0},{2,2,5});
        check(near(materialVolume(sphere),4*pi/3),"unit-radius sphere analytic volume");
        check(near(materialVolume(cylinder),5*pi),"unit-radius length-five cylinder analytic volume");
        check(near(surfaceArea(sphere),4*pi),"unit-radius sphere surface area");
        check(near(surfaceArea(cylinder),12*pi),"unit-radius length-five cylinder surface area");
        auto box=shape("box","box",{0,0,0},{2,3,4});
        check(near(materialVolume(box),24)&&near(surfaceArea(box),52),"box analytic volume and area remain compatible");
        auto shell=box;shell.size={10,10,10};shell.wall=1;
        check(near(materialVolume(shell),488),"legacy hollow-box material volume keeps six-wall semantics");

        // A rotated local-Z cylinder has an independently calculated world AABB.
        Transform rotated;rotated.rotation={{{0,0,1},{0,1,0},{-1,0,0}}};rotated.translation={3,4,5};
        auto bounds=worldBounds(cylinder,rotated);
        check(near(bounds[0],0.5)&&near(bounds[1],5.5)&&near(bounds[2],3)&&near(bounds[3],5)&&near(bounds[4],4)&&near(bounds[5],6),"cylinder AABB follows analytic local-Z support after RY90");
        auto sphereBounds=worldBounds(sphere,rotated);
        check(near(sphereBounds[0],2)&&near(sphereBounds[1],4)&&near(sphereBounds[2],3)&&near(sphereBounds[3],5)&&near(sphereBounds[4],4)&&near(sphereBounds[5],6),"sphere bounds are rotation-invariant around world center");

        auto curvedClearance=emptyProject(10);
        curvedClearance.volumes={shape("edge_sphere","sphere",{8.8,0,0},{2,2,2},true)};
        try{validateGeometry(curvedClearance);check(true,"source sphere uses exact curved radius instead of box-corner bound");}
        catch(...){check(false,"source sphere uses exact curved radius instead of box-corner bound");}

        // Convex overlap regression pairs. Separated cases leave a material gap;
        // overlap cases penetrate enough to avoid ambiguous numerical contact.
        rejects([&]{validateGeometry(two(shape("s1","sphere",{0,0,0},{2,2,2},true),shape("s2","sphere",{1.5,0,0},{2,2,2})));},"overlapping sphere/sphere rejected");
        try{validateGeometry(two(shape("s1","sphere",{0,0,0},{2,2,2},true),shape("s2","sphere",{3,0,0},{2,2,2})));check(true,"separated sphere/sphere accepted");}
        catch(...){check(false,"separated sphere/sphere accepted");}
        rejects([&]{validateGeometry(two(shape("s","sphere",{0,0,0},{2,2,2},true),shape("b","box",{1.3,0,0},{1,1,1})));},"overlapping sphere/box rejected");
        try{validateGeometry(two(shape("s","sphere",{0,0,0},{2,2,2},true),shape("b","box",{2.2,0,0},{1,1,1})));check(true,"separated sphere/box accepted");}
        catch(...){check(false,"separated sphere/box accepted");}
        rejects([&]{validateGeometry(two(shape("s","sphere",{0,0,0},{2,2,2},true),shape("c","cylinder",{1.2,0,0},{1,1,4})));},"overlapping sphere/cylinder rejected");
        try{validateGeometry(two(shape("s","sphere",{0,0,0},{2,2,2},true),shape("c","cylinder",{2,0,0},{1,1,4})));check(true,"separated sphere/cylinder accepted");}
        catch(...){check(false,"separated sphere/cylinder accepted");}
        rejects([&]{validateGeometry(two(shape("c1","cylinder",{0,0,0},{1,1,4},true),shape("c2","cylinder",{0.8,0,0},{1,1,4})));},"overlapping parallel cylinders rejected");
        try{validateGeometry(two(shape("c1","cylinder",{0,0,0},{1,1,4},true),shape("c2","cylinder",{1.3,0,0},{1,1,4})));check(true,"separated parallel cylinders accepted");}
        catch(...){check(false,"separated parallel cylinders accepted");}
        try{validateGeometry(two(shape("c1","cylinder",{0,0,0},{2,2,4},true),shape("c2","cylinder",{1.5,1.5,0},{2,2,4})));check(true,"GJK accepts diagonal parallel cylinders with overlapping OBBs but disjoint circular sections");}
        catch(...){check(false,"GJK accepts diagonal parallel cylinders with overlapping OBBs but disjoint circular sections");}
        try{validateGeometry(two(shape("c","cylinder",{0,0,0},{2,2,4},true),shape("b","box",{1,1,0},{0.2,0.2,1})));check(true,"GJK accepts cylinder and box with overlapping OBBs but a curved-surface gap");}
        catch(...){check(false,"GJK accepts cylinder and box with overlapping OBBs but a curved-surface gap");}
        rejects([&]{validateGeometry(two(shape("cz","cylinder",{0,0,0},{1,1,4},true),shape("cx","cylinder",{0,0,0},{1,1,4},false,{90,0,0})));},"crossing cylinders rejected");

        auto cavity=emptyProject();auto hollow=shape("hollow","box",{0,0,0},{10,10,10});hollow.wall=1;
        cavity.volumes={hollow,shape("ball","sphere",{0,0,0},{2,2,2},true)};
        try{validateGeometry(cavity);check(true,"curved sensitive body inside hollow box cavity is legal");}
        catch(...){check(false,"curved sensitive body inside hollow box cavity is legal");}
        auto badWall=sphere;badWall.wall=0.1;
        rejects([&]{validateVolumeShape(badWall);},"curved shell with nonzero wall rejected");
        auto badCylinder=cylinder;badCylinder.size={2,3,5};
        rejects([&]{validateVolumeShape(badCylinder);},"cylinder with mismatched diameters rejected");

        // Project v6 stores shape tags on every physical row, including boxes, and
        // uses the v2 component-template contract for templates containing curves.
        Project analytic=Project::shapeDemo();Json v6=analytic.toJson();
        check(v6["schema"]=="scdat.radiation/6"&&v6["geometry_contract"]=="scdat.analytic-assembly/1","curved authored/instance geometry selects v6 contract");
        check(v6["volumes"].size()==3&&v6["volumes"][0]["shape"]=="box"&&v6["volumes"][1]["shape"]=="sphere"&&v6["volumes"][2]["shape"]=="cylinder","v6 tags each explicit shape including box");
        check(v6["templates"].size()==1&&v6["templates"][0]["schema"]=="scdat.component-template/2","curved template upgrades to template contract v2");
        check(Project::fromJson(v6).toJson()==v6,"v6 project with instances and shapes round-trips");
        auto expanded=compileGeometry(analytic);
        check(expanded.volumes.size()==7&&expanded.volumes[3].id=="Sensor_A/Ball_Si"&&expanded.volumes[3].shape=="sphere"&&expanded.volumes[4].shape=="cylinder","analytic shapes survive instance expansion with stable IDs");
        auto boxesOnly=Project::instanceDemo().templates[0];Json templateV1=templateToJson(boxesOnly);
        check(templateV1["schema"]=="scdat.component-template/1","box-only template retains v1 contract");
        auto curveTemplate=analytic.templates[0];Json templateV2=templateToJson(curveTemplate);
        check(templateV2["schema"]=="scdat.component-template/2"&&templateToJson(templateFromJson(templateV2))==templateV2,"analytic template v2 round-trip");
        Json mislabeledTemplate=templateV2;mislabeledTemplate["schema"]="scdat.component-template/1";
        rejects([&]{templateFromJson(mislabeledTemplate);},"template v1 rejects shape fields");

        Json old1=legacyV1();old1["volumes"][0]["shape"]="sphere";
        Json old2=Project::demo().toJson();old2["volumes"][0]["shape"]="sphere";
        Json old3=Project::assemblyDemo().toJson();old3["volumes"][0]["shape"]="sphere";
        auto pinned=emptyProject();pinned.volumes={shape("si","box",{0,0,0},{2,2,1},true)};pinned.materialSnapshot=siliconSnapshot();Json old4=pinned.toJson();old4["volumes"][0]["shape"]="sphere";
        Json old5=Project::instanceDemo().toJson();old5["volumes"][0]["shape"]="sphere";
        rejects([&]{Project::fromJson(old1);},"schema v1 rejects shape tag");
        rejects([&]{Project::fromJson(old2);},"schema v2 rejects shape tag");
        rejects([&]{Project::fromJson(old3);},"schema v3 rejects shape tag");
        rejects([&]{Project::fromJson(old4);},"schema v4 rejects shape tag");
        rejects([&]{Project::fromJson(old5);},"schema v5 rejects shape tag");

        std::cout<<"analytic shape checks: "<<checks<<" passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
