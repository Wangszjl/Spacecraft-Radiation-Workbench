#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
bool convexOverlap(const Volume&,const Transform&,const Volume&,const Transform&);
namespace {
constexpr double tolerance=1e-8; // mm; contact and penetrations <= tolerance are accepted
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
Vector3 subtract(const Vector3& a,const Vector3& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
double dot(const Vector3& a,const Vector3& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vector3 cross(const Vector3& a,const Vector3& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vector3 axis(const Matrix3& r,int i){return {r[0][i],r[1][i],r[2][i]};}
Transform localTransform(const Vector3& center,const Vector3& angles){
    for(int i=0;i<3;++i)require(std::isfinite(center[i])&&std::isfinite(angles[i]),"Nonfinite geometry position/rotation");
    // Reduce angles first to avoid overflow converting degrees to radians.
    double x=std::remainder(angles[0],360.0)*pi/180,y=std::remainder(angles[1],360.0)*pi/180,z=std::remainder(angles[2],360.0)*pi/180;
    double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {{{{cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx},
              {sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx},
              {-sy,cy*sx,cy*cx}}},center};
}
Transform compose(const Transform& parent,const Transform& child){
    Transform t;t.translation=parent.apply(child.translation);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){t.rotation[i][j]=0;for(int k=0;k<3;++k)t.rotation[i][j]+=parent.rotation[i][k]*child.rotation[k][j];}
    for(double v:t.translation)require(std::isfinite(v),"World geometry transform overflow");
    return t;
}
struct OrientedBox {Vector3 center, half;Matrix3 rotation;};
struct MaterialPart {Volume volume;Transform transform;OrientedBox bounds;};
bool overlaps(const OrientedBox& a,const OrientedBox& b){
    auto delta=subtract(b.center,a.center);
    auto separates=[&](Vector3 n){
        double norm=std::sqrt(dot(n,n));if(norm<1e-12)return false;
        for(double& c:n)c/=norm;
        double reach=0;for(int i=0;i<3;++i)reach+=a.half[i]*std::abs(dot(n,axis(a.rotation,i)))+b.half[i]*std::abs(dot(n,axis(b.rotation,i)));
        return reach-std::abs(dot(delta,n))<=tolerance;
    };
    // Separating axis theorem: six face normals and nine edge cross products.
    for(int i=0;i<3;++i)if(separates(axis(a.rotation,i))||separates(axis(b.rotation,i)))return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(separates(cross(axis(a.rotation,i),axis(b.rotation,j))))return false;
    return true;
}
std::vector<MaterialPart> partsFor(const Volume& v,const Transform& transform){
    std::vector<MaterialPart> result;
    if(v.shape=="box")for(const auto& box:materialBoxes(v)){
        auto part=v;part.size=box.size;part.wall=0;auto world=transform;world.translation=transform.apply(box.center);
        Vector3 half=box.size;for(auto& h:half)h/=2;result.push_back({part,world,{world.translation,half,world.rotation}});
    }else{auto half=v.size;for(auto& h:half)h/=2;result.push_back({v,transform,{transform.translation,half,transform.rotation}});}
    return result;
}
bool partsConflict(const std::vector<MaterialPart>& a,const std::vector<MaterialPart>& b){
    for(const auto& left:a)for(const auto& right:b){
        if(!overlaps(left.bounds,right.bounds))continue;
        if(left.volume.shape=="box"&&right.volume.shape=="box")return true;
        if(convexOverlap(left.volume,left.transform,right.volume,right.transform))return true;
    }return false;
}
}
Vector3 Transform::apply(const Vector3& v)const{
    auto out=translation;for(int i=0;i<3;++i)for(int j=0;j<3;++j)out[i]+=rotation[i][j]*v[j];return out;
}
static std::vector<ResolvedVolume> resolveModel(const GeometryModel& p){
    require(p.assemblies.size()+p.volumes.size()<=4096,"Geometry exceeds 4096 nodes");
    std::set<std::string> ids{"World"};std::map<std::string,size_t> index;
    for(size_t i=0;i<p.assemblies.size();++i){const auto& a=p.assemblies[i];
        require(!a.id.empty()&&ids.insert(a.id).second,"Geometry identifiers must be nonempty, unique and not World: "+a.id);index[a.id]=i;}
    for(const auto& v:p.volumes)require(!v.id.empty()&&ids.insert(v.id).second,"Geometry identifiers must be nonempty, unique and not World: "+v.id);
    std::vector<int> mark(p.assemblies.size()),levels(p.assemblies.size(),1);std::vector<Transform> world(p.assemblies.size());std::vector<bool> shown(p.assemblies.size());
    std::function<void(size_t,int)> visit=[&](size_t i,int depth){
        require(depth<=128,"Assembly hierarchy exceeds 128 levels");require(mark[i]!=1,"Assembly cycle at: "+p.assemblies[i].id);if(mark[i]==2)return;
        mark[i]=1;const auto& a=p.assemblies[i];auto t=localTransform(a.center,a.rotation);bool visible=a.visible;
        if(!a.parent.empty()){auto it=index.find(a.parent);require(it!=index.end(),"Unknown parent assembly: "+a.parent);visit(it->second,depth+1);t=compose(world[it->second],t);visible=visible&&shown[it->second];levels[i]=levels[it->second]+1;}
        require(levels[i]<=128,"Assembly hierarchy exceeds 128 levels");
        world[i]=t;shown[i]=visible;mark[i]=2;
    };
    for(size_t i=0;i<p.assemblies.size();++i)visit(i,1);
    std::vector<ResolvedVolume> result;result.reserve(p.volumes.size());
    for(size_t i=0;i<p.volumes.size();++i){const auto& v=p.volumes[i];auto t=localTransform(v.center,v.rotation);bool visible=v.visible;
        if(!v.parent.empty()){auto it=index.find(v.parent);require(it!=index.end(),"Unknown parent assembly: "+v.parent);t=compose(world[it->second],t);visible=visible&&shown[it->second];}
        result.push_back({i,t,visible});}
    return result;
}
CompiledGeometry compileGeometry(const Project& p){
    auto model=expandInstances(p);CompiledGeometry geometry;
    geometry.volumes=std::move(model.volumes);geometry.assemblies=std::move(model.assemblies);geometry.origins=std::move(model.origins);
    geometry.placements=resolveModel(geometry);return geometry;
}
std::vector<ResolvedVolume> resolveGeometry(const Project& p){return compileGeometry(p).placements;}
std::vector<MaterialBox> materialBoxes(const Volume& v){
    require(v.shape=="box","Box wall decomposition cannot represent curved solids: "+v.id);
    if(v.wall==0)return {{{0,0,0},v.size}};
    // Disjoint slabs (shared boundaries only), exactly covering outer minus cavity.
    std::vector<MaterialBox> boxes;auto inner=v.size;
    for(int k=0;k<3;++k)inner[k]-=2*v.wall;
    for(int k=0;k<3;++k)for(int sign:{-1,1}){
        Vector3 center{},size=v.size;center[k]=sign*(v.size[k]-v.wall)/2;size[k]=v.wall;
        for(int j=0;j<k;++j)size[j]=inner[j];boxes.push_back({center,size});}
    return boxes;
}
void validateGeometry(const Project& p){
    auto geometry=compileGeometry(p);require(!geometry.volumes.empty(),"Geometry is empty");
    const auto& resolved=geometry.placements;bool target=false;
    std::vector<std::vector<MaterialPart>> parts(geometry.volumes.size());
    for(const auto& r:resolved){const auto& v=geometry.volumes[r.index];
        require(v.material.rfind("G4_",0)==0,"Use a Geant4 NIST material identifier: "+v.id);
        validateVolumeShape(v);
        require(outerRadiusFromOrigin(v,r.transform)<p.radius*0.99,"Source sphere must enclose all geometry with 1% clearance: "+v.id);
        target|=v.sensitive;
        parts[r.index]=partsFor(v,r.transform);
    }
    require(target,"At least one solid sensitive volume is required");
    for(size_t i=0;i<parts.size();++i)for(size_t j=0;j<i;++j)
        require(!partsConflict(parts[i],parts[j]),"Overlapping material volumes or unresolved curved contact: "+geometry.volumes[i].id+" / "+geometry.volumes[j].id);
}
bool hasAssemblyGeometry(const Project& p){
    if(!p.assemblies.empty())return true;
    for(const auto& v:p.volumes)if(!v.parent.empty()||v.rotation!=Vector3{}||!v.visible)return true;
    return false;
}
Json geometryDiagnostics(const Project& p,size_t maximumIssues,size_t maximumPairs){
    require(maximumIssues>=1&&maximumIssues<=4096,"Diagnostic issue budget must be 1..4096");
    require(maximumPairs<=100000,"Diagnostic pair budget must be <=100000");
    Json out={{"schema","scdat.radiation-geometry-diagnostics/1"},{"scope","analytic geometry only; not transport approval"},{"length_unit","mm"},
        {"unit_status","declared by internal model contract; CAD source units not verified"},{"status","incomplete"},{"complete",false},{"topology_resolved",false},
        {"issues",Json::array()},{"volumes",Json::array()},{"maximum_issues",maximumIssues},{"maximum_pairs",maximumPairs},
        {"issue_limit_reached",false},{"pair_limit_reached",false},{"possible_pair_count",0},{"checked_pair_count",0},{"skipped_pair_count",0},{"unresolved_pair_count",0},{"unexamined_pair_count",0},
        {"overlap_tolerance_mm",tolerance},{"preview_coordinate_limit_mm",1e9},
        {"unchecked",{"CAD source units","mesh closure","mesh normals","degenerate triangles","mesh self intersections","triangulation volume/mass convergence","NIST catalogue/composition","full project/environment/runtime validation"}}};
    auto issue=[&](const std::string& code,const Json& ids,const std::string& detail){
        if(out["issues"].size()>=maximumIssues){out["issue_limit_reached"]=true;return;}
        out["issues"].push_back({{"code",code},{"severity","error"},{"related_ids",ids},{"detail",detail}});
    };
    CompiledGeometry geometry;
    try{geometry=compileGeometry(p);out["topology_resolved"]=true;}
    catch(const std::exception& e){issue("topology_unresolved",Json::array(),e.what());out["status"]="invalid";return out;}
    if(geometry.volumes.empty())issue("geometry_empty",Json::array(),"Geometry is empty");
    const bool radius=std::isfinite(p.radius)&&p.radius>0;
    if(!radius)issue("source_radius_invalid",Json::array(),"Source radius must be finite and positive");
    bool target=false,allShapesValid=true;std::vector<std::vector<MaterialPart>> parts(geometry.volumes.size());
    for(const auto& resolved:geometry.placements){const auto& v=geometry.volumes[resolved.index];target|=v.sensitive;
        if(v.material.rfind("G4_",0)!=0)issue("material_identifier",{v.id},"Use a Geant4 NIST material identifier: "+v.id);
        bool valid=true;try{validateVolumeShape(v);}catch(const std::exception& e){valid=false;allShapesValid=false;issue("shape_invalid",{v.id},e.what());}
        bool renderable=false;Json bounds=nullptr;
        if(valid){
            parts[resolved.index]=partsFor(v,resolved.transform);
            auto box=worldBounds(v,resolved.transform);renderable=std::all_of(box.begin(),box.end(),[](double x){return std::isfinite(x)&&std::abs(x)<=1e9;});if(renderable)bounds=box;
            if(radius&&!(outerRadiusFromOrigin(v,resolved.transform)<p.radius*.99))issue("source_clearance",{v.id},"Source sphere must enclose geometry with 1% clearance: "+v.id);
        }
        out["volumes"].push_back({{"id",v.id},{"shape_valid",valid},{"renderable",renderable},{"world_bounds_mm",bounds},{"material",v.material},{"sensitive",v.sensitive},{"visible",resolved.visible}});
    }
    if(!target)issue("sensitive_volume_missing",Json::array(),"At least one solid sensitive volume is required");
    const size_t n=parts.size(),possible=n*(n? n-1:0)/2;size_t checked=0,skipped=0,unresolved=0;bool stop=false;
    for(size_t i=0;i<n&&!stop;++i)for(size_t j=0;j<i;++j){
        if(out["issue_limit_reached"]==true){stop=true;break;}
        if(parts[i].empty()||parts[j].empty()){++skipped;continue;}
        if(checked>=maximumPairs){out["pair_limit_reached"]=true;stop=true;break;}
        ++checked;
        try{if(partsConflict(parts[i],parts[j]))issue("material_overlap_or_unresolved_contact",{geometry.volumes[i].id,geometry.volumes[j].id},"Overlapping material volumes or unresolved curved contact");}
        catch(const std::exception& e){++unresolved;issue("intersection_unresolved",{geometry.volumes[i].id,geometry.volumes[j].id},e.what());}
    }
    out["possible_pair_count"]=possible;out["checked_pair_count"]=checked;out["skipped_pair_count"]=skipped;out["unexamined_pair_count"]=possible-checked-skipped;
    out["unresolved_pair_count"]=unresolved;
    const bool complete=radius&&allShapesValid&&skipped==0&&unresolved==0&&possible==checked&&out["issue_limit_reached"]==false;
    out["complete"]=complete;out["status"]=!out["issues"].empty()?"invalid":complete?"valid":"incomplete";return out;
}
Json geometrySnapshot(const Project& p){
    auto geometry=compileGeometry(p);Json rows=Json::array();for(const auto& r:geometry.placements){const auto& v=geometry.volumes[r.index];
        double volume=materialVolume(v);
        require(std::isfinite(volume)&&volume>0,"Material volume overflow: "+v.id);
        rows.push_back({{"id",v.id},{"parent",v.parent},{"material",v.material},{"sensitive",v.sensitive},
            {"visible",r.visible},{"local_center_mm",v.center},{"local_rotation_deg",v.rotation},
            {"world_center_mm",r.transform.translation},{"world_rotation",r.transform.rotation},
            {"size_mm",v.size},{"wall_mm",v.wall},{"material_volume_mm3",volume}});
        if(hasAnalyticGeometry(p)){rows.back()["shape"]=v.shape;rows.back()["analytic_world_bounds_mm"]=worldBounds(v,r.transform);}
        if(geometry.origins.contains(v.id))rows.back()["instance_origin"]=geometry.origins.at(v.id);}
    return {{"contract",hasAnalyticGeometry(p)?"scdat.analytic-assembly/1":"scdat.box-assembly/1"},{"transform_convention","active column vector; Rz*Ry*Rx; parent*local"},
        {"placement","resolved world siblings; assemblies have no material"},{"visibility","preview only; hidden volumes remain in transport"},
        {"overlap_tolerance_mm",tolerance},{"volumes",rows}};
}
}
