#include "PreviewMeshQuality.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>
using namespace spacerad::engine;
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
TriangleMesh tetra(){return {{{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}}};}
int main(){try{
    auto t=tetra();auto a=auditTriangleMesh(t);check(a.at("closed_oriented_surface"),"tetra closed");check(std::abs(a.at("signed_volume_mm3").get<double>()-1./6)<1e-14,"tetra volume");check(a.at("edge_connected_component_count")==1,"one component");
    auto open=t;open.triangles.pop_back();a=auditTriangleMesh(open);check(a.at("boundary_edge_count")==3&&!a.at("closed_oriented_surface").get<bool>(),"open detected");check(a.at("signed_volume_mm3").is_null(),"open volume untrusted");
    auto reverse=t;std::swap(reverse.triangles[0][0],reverse.triangles[0][1]);a=auditTriangleMesh(reverse);check(a.at("inconsistent_orientation_edge_count")==3,"one reversed face");
    auto inward=t;for(auto& f:inward.triangles)std::swap(f[0],f[1]);a=auditTriangleMesh(inward);check(a.at("closed_oriented_surface")&&a.at("signed_volume_mm3").get<double>()<0,"negative winding explicit, nesting not inferred");
    auto deg=t;deg.triangles.push_back({0,0,1});a=auditTriangleMesh(deg);check(a.at("degenerate_triangle_count")==1&&!a.at("closed_oriented_surface").get<bool>(),"degenerate detected");
    auto duplicate=t;duplicate.triangles.push_back(t.triangles[0]);a=auditTriangleMesh(duplicate);check(a.at("duplicate_triangle_count")==1&&a.at("nonmanifold_edge_count")==3,"duplicate nonmanifold");
    TriangleMesh split;for(auto f:t.triangles){std::array<std::size_t,3> tri;for(int k=0;k<3;++k){tri[k]=split.points.size();split.points.push_back(t.points[f[k]]);}split.triangles.push_back(tri);}a=auditTriangleMesh(split);check(a.at("point_count")==12&&a.at("welded_point_count")==4&&a.at("closed_oriented_surface"),"exact duplicate coordinates welded for analysis");check(split.points.size()==12,"input unchanged");
    auto far=t;for(auto& p:far.points){p[0]+=1e9;p[1]-=1e9;p[2]+=1e9;}a=auditTriangleMesh(far);check(std::abs(a.at("signed_volume_mm3").get<double>()-1./6)<1e-14,"translation stable volume");
    auto scale=t;for(auto& p:scale.points)for(auto& x:p)x*=1e-8;a=auditTriangleMesh(scale);check(a.at("closed_oriented_surface"),"small mesh scale relative threshold");check(std::abs(a.at("signed_volume_mm3").get<double>()/(1e-24/6)-1)<1e-12,"small volume scale");
    auto two=t;for(auto p:t.points){p[0]+=3;two.points.push_back(p);}for(auto f:t.triangles){for(auto& i:f)i+=4;two.triangles.push_back(f);}a=auditTriangleMesh(two);check(a.at("edge_connected_component_count")==2&&a.at("closed_oriented_surface"),"disconnected closed components");
    auto touch=t;for(auto p:t.points){for(auto& x:p)x=-x;touch.points.push_back(p);}for(auto f:t.triangles){for(auto& i:f)i+=4;std::swap(f[0],f[1]);touch.triangles.push_back(f);}a=auditTriangleMesh(touch);check(a.at("nonmanifold_edge_count")==0&&a.at("nonmanifold_vertex_count")==1&&!a.at("closed_oriented_surface").get<bool>(),"vertex pinch detected");
    auto invalid=t;invalid.triangles[0][0]=99;bool rejects=false;try{auditTriangleMesh(invalid);}catch(const std::exception&){rejects=true;}check(rejects,"index checked");invalid=t;invalid.points[0][0]=std::numeric_limits<double>::infinity();rejects=false;try{auditTriangleMesh(invalid);}catch(const std::exception&){rejects=true;}check(rejects,"nonfinite rejected");
    a=auditTriangleMesh({});check(!a.at("closed_oriented_surface").get<bool>()&&a.at("signed_volume_mm3").is_null(),"empty cannot pass");
    std::cout<<checks<<" preview mesh checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
