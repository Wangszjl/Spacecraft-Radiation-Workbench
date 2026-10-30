#include "PreviewMeshQuality.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
struct Sets {
    std::vector<std::size_t> p;
    explicit Sets(std::size_t n):p(n){std::iota(p.begin(),p.end(),0);}
    std::size_t root(std::size_t i){while(p[i]!=i){p[i]=p[p[i]];i=p[i];}return i;}
    void join(std::size_t a,std::size_t b){p[root(a)]=root(b);}
};
using LVec=std::array<long double,3>;
LVec sub(const Vector3& a,const Vector3& b){return {static_cast<long double>(a[0])-b[0],static_cast<long double>(a[1])-b[1],static_cast<long double>(a[2])-b[2]};}
LVec cross(const LVec& a,const LVec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
long double dot(const LVec& a,const LVec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double number(long double x){double v=static_cast<double>(x);if(!std::isfinite(v))throw std::runtime_error("Mesh arithmetic overflow");return v;}
}
Json auditTriangleMesh(const TriangleMesh& input){
    if(input.points.size()>1000000||input.triangles.size()>2000000)throw std::runtime_error("Mesh audit budget exceeded (1M points / 2M triangles)");
    std::map<Vector3,std::size_t> unique;std::vector<Vector3> points;std::vector<std::size_t> remap;
    for(const auto& p:input.points){for(double v:p)if(!std::isfinite(v))throw std::runtime_error("Nonfinite mesh point");auto it=unique.emplace(p,points.size());if(it.second)points.push_back(p);remap.push_back(it.first->second);}
    Vector3 lo{},hi{};if(!points.empty())lo=hi=points.front();
    for(const auto& p:points)for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
    auto diagonal=sub(hi,lo);long double threshold=dot(diagonal,diagonal)*1e-14L;
    struct Use {std::size_t face;bool forward;};using Edge=std::pair<std::size_t,std::size_t>;
    std::map<Edge,std::vector<Use>> edges;std::vector<std::array<std::size_t,3>> faces;
    std::set<std::array<std::size_t,3>> canonical;std::set<std::size_t> used;
    std::size_t degenerates=0,duplicates=0;long double area=0;
    for(auto t:input.triangles){for(auto& i:t){if(i>=remap.size())throw std::runtime_error("Triangle vertex index out of range");i=remap[i];used.insert(i);}
        auto n=cross(sub(points[t[1]],points[t[0]]),sub(points[t[2]],points[t[0]]));long double twiceArea=std::sqrt(dot(n,n));
        if(t[0]==t[1]||t[1]==t[2]||t[2]==t[0]||twiceArea<=threshold){++degenerates;continue;}
        area+=twiceArea/2;auto key=t;std::sort(key.begin(),key.end());if(!canonical.insert(key).second)++duplicates;
        auto face=faces.size();faces.push_back(t);for(int k=0;k<3;++k){auto a=t[k],b=t[(k+1)%3];edges[std::minmax(a,b)].push_back({face,a<b});}
    }
    Sets components(faces.size());std::size_t boundary=0,nonmanifold=0,inconsistent=0;
    std::vector<std::vector<std::size_t>> incident(points.size());
    for(std::size_t f=0;f<faces.size();++f)for(auto v:faces[f])incident[v].push_back(f);
    std::vector<std::vector<std::pair<std::size_t,std::size_t>>> links(points.size());
    for(const auto& entry:edges){const auto& uses=entry.second;if(uses.size()==1)++boundary;if(uses.size()>2)++nonmanifold;
        if(uses.size()==2&&uses[0].forward==uses[1].forward)++inconsistent;
        for(std::size_t j=1;j<uses.size();++j){components.join(uses[0].face,uses[j].face);for(auto v:{entry.first.first,entry.first.second})links[v].push_back({uses[0].face,uses[j].face});}
    }
    // Edge multiplicity alone misses two closed shells touching at one vertex.
    std::size_t nonmanifoldVertices=0;
    for(std::size_t v=0;v<points.size();++v){const auto& fan=incident[v];if(fan.empty())continue;Sets local(fan.size());
        auto index=[&](std::size_t f){return std::size_t(std::lower_bound(fan.begin(),fan.end(),f)-fan.begin());};
        for(auto link:links[v])local.join(index(link.first),index(link.second));std::set<std::size_t> roots;for(std::size_t j=0;j<fan.size();++j)roots.insert(local.root(j));if(roots.size()!=1)++nonmanifoldVertices;
    }
    std::map<std::size_t,std::vector<std::size_t>> groups;for(std::size_t f=0;f<faces.size();++f)groups[components.root(f)].push_back(f);
    Json componentRows=Json::array();long double totalVolume=0;
    for(const auto& group:groups){const auto& list=group.second;Vector3 min=points[faces[list[0]][0]],max=min;
        for(auto f:list)for(auto v:faces[f])for(int k=0;k<3;++k){min[k]=std::min(min[k],points[v][k]);max[k]=std::max(max[k],points[v][k]);}
        Vector3 origin{};for(int k=0;k<3;++k)origin[k]=min[k]/2+max[k]/2;long double volume=0;
        for(auto f:list){auto t=faces[f];volume+=dot(sub(points[t[0]],origin),cross(sub(points[t[1]],origin),sub(points[t[2]],origin)))/6;}
        totalVolume+=volume;componentRows.push_back({{"triangle_count",list.size()},{"raw_signed_volume_mm3",number(volume)}});
    }
    bool closed=!faces.empty()&&boundary==0&&nonmanifold==0&&nonmanifoldVertices==0&&inconsistent==0&&duplicates==0&&degenerates==0;
    return {{"schema","scdat.radiation-triangle-quality/1"},{"coordinate_unit","mm"},{"welding","exact_coordinate_analysis_only"},
        {"point_count",input.points.size()},{"welded_point_count",points.size()},{"unused_welded_point_count",points.size()-used.size()},
        {"triangle_count",input.triangles.size()},{"degenerate_triangle_count",degenerates},{"duplicate_triangle_count",duplicates},
        {"twice_area_degeneracy_threshold_mm2",number(threshold)},{"boundary_edge_count",boundary},{"nonmanifold_edge_count",nonmanifold},
        {"nonmanifold_vertex_count",nonmanifoldVertices},{"inconsistent_orientation_edge_count",inconsistent},
        {"edge_connected_component_count",groups.size()},{"components",componentRows},{"surface_area_mm2",number(area)},
        {"closed_oriented_surface",closed},{"raw_signed_volume_mm3",number(totalVolume)},
        {"signed_volume_mm3",closed?Json(number(totalVolume)):Json(nullptr)},
        {"unchecked",{"self_intersection","inter_component_intersection_or_nesting","material_outward_normals","stored_point_normals","cad_units"}}};
}
}
