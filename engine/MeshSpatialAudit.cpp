#include "MeshSpatialAudit.h"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
// Error-free TwoSum and FMA TwoProduct, accumulated with grow-expansion.
// No guessed epsilon in orientation signs. The entry-point domain prevents
// overflow/underflow of the degree <=3 polynomials and their residuals.
using Expansion=std::vector<double>;
void grow(Expansion& e,double b){if(b==0)return;Expansion out;out.reserve(e.size()+1);for(double a:e){double sum=a+b,bv=sum-a,av=sum-bv,br=b-bv,ar=a-av,error=ar+br;if(error!=0)out.push_back(error);b=sum;}if(b!=0)out.push_back(b);e=std::move(out);}
Expansion scalar(double x){return x==0?Expansion{}:Expansion{x};}
Expansion add(Expansion a,const Expansion& b,int sign=1){for(double x:b)grow(a,sign*x);return a;}
Expansion mul(const Expansion& a,const Expansion& b){Expansion out;for(double x:a)for(double y:b){double hi=x*y,lo=std::fma(x,y,-hi);grow(out,lo);grow(out,hi);}return out;}
Expansion difference(double a,double b){auto out=scalar(a);grow(out,-b);return out;}
int sign(const Expansion& e){return e.empty()?0:e.back()>0?1:-1;}
int orient2(const Vector3& a,const Vector3& b,const Vector3& c,int drop){int x=(drop+1)%3,y=(drop+2)%3;return sign(add(mul(difference(a[x],c[x]),difference(b[y],c[y])),mul(difference(a[y],c[y]),difference(b[x],c[x])),-1));}
Expansion determinant3(const Vector3& a,const Vector3& b,const Vector3& c,const Vector3& d){std::array<Expansion,3> x,y,z;for(int k=0;k<3;++k){x[k]=difference(a[k],d[k]);y[k]=difference(b[k],d[k]);z[k]=difference(c[k],d[k]);}auto yz=[&](int i,int j){return add(mul(y[i],z[j]),mul(y[j],z[i]),-1);};return add(add(mul(x[0],yz(1,2)),mul(x[1],yz(0,2)),-1),mul(x[2],yz(0,1)));}
int orient3(const Vector3& a,const Vector3& b,const Vector3& c,const Vector3& d){return sign(determinant3(a,b,c,d));}
using Face=std::array<std::size_t,3>;
struct Box {Vector3 lo{},hi{};};
Box unite(Box a,const Box& b){for(int k=0;k<3;++k){a.lo[k]=std::min(a.lo[k],b.lo[k]);a.hi[k]=std::max(a.hi[k],b.hi[k]);}return a;}
bool overlap(const Box& a,const Box& b){for(int k=0;k<3;++k)if(a.hi[k]<b.lo[k]||b.hi[k]<a.lo[k])return false;return true;}
bool contains(const Box& b,const Vector3& p){for(int k=0;k<3;++k)if(p[k]<b.lo[k]||p[k]>b.hi[k])return false;return true;}
struct Sets {std::vector<std::size_t> p;explicit Sets(std::size_t n):p(n){std::iota(p.begin(),p.end(),0);}std::size_t root(std::size_t i){while(i!=p[i]){p[i]=p[p[i]];i=p[i];}return i;}void join(std::size_t a,std::size_t b){p[root(a)]=root(b);}};
struct Triangle {Face vertices;Box box;int projection=-1;};
bool allSame(int a,int b,int c){return (a>=0&&b>=0&&c>=0)||(a<=0&&b<=0&&c<=0);}
bool onSegment(const Vector3& a,const Vector3& b,const Vector3& p,int drop){if(orient2(a,b,p,drop)!=0)return false;for(int k=0;k<3;++k)if(k!=drop&&(p[k]<std::min(a[k],b[k])||p[k]>std::max(a[k],b[k])))return false;return true;}
bool inTriangle(const Vector3& p,const std::array<Vector3,3>& t,int drop){return allSame(orient2(t[0],t[1],p,drop),orient2(t[1],t[2],p,drop),orient2(t[2],t[0],p,drop));}
bool allowed(const Vector3& p,const std::vector<Vector3>& shared){return std::find(shared.begin(),shared.end(),p)!=shared.end();}
bool segmentCoplanarBeyond(const Vector3& a,const Vector3& b,const std::array<Vector3,3>& t,int drop,const std::vector<Vector3>& shared){
    if((inTriangle(a,t,drop)&&!allowed(a,shared))||(inTriangle(b,t,drop)&&!allowed(b,shared)))return true;
    for(int i=0;i<3;++i){const auto& c=t[i];const auto& d=t[(i+1)%3];int s1=orient2(a,b,c,drop),s2=orient2(a,b,d,drop),s3=orient2(c,d,a,drop),s4=orient2(c,d,b,drop);
        if(s1*s2<0&&s3*s4<0)return true;
        if((s1==0&&onSegment(a,b,c,drop)&&!allowed(c,shared))||(s2==0&&onSegment(a,b,d,drop)&&!allowed(d,shared))||(s3==0&&onSegment(c,d,a,drop)&&!allowed(a,shared))||(s4==0&&onSegment(c,d,b,drop)&&!allowed(b,shared)))return true;
    }return false;
}
bool segmentBeyond(const Vector3& a,const Vector3& b,const std::array<Vector3,3>& t,int projection,const std::vector<Vector3>& shared){
    int sa=orient3(t[0],t[1],t[2],a),sb=orient3(t[0],t[1],t[2],b);if(sa*sb>0)return false;if(sa==0&&sb==0)return segmentCoplanarBeyond(a,b,t,projection,shared);
    if(sa==0)return inTriangle(a,t,projection)&&!allowed(a,shared);if(sb==0)return inTriangle(b,t,projection)&&!allowed(b,shared);
    return allSame(orient3(a,b,t[0],t[1]),orient3(a,b,t[1],t[2]),orient3(a,b,t[2],t[0]));
}
bool intersectsBeyond(const Triangle& a,const Triangle& b,const std::vector<Vector3>& points){
    std::vector<Vector3> shared;for(auto i:a.vertices)for(auto j:b.vertices)if(i==j)shared.push_back(points[i]);std::array<Vector3,3> pa,pb;for(int k=0;k<3;++k){pa[k]=points[a.vertices[k]];pb[k]=points[b.vertices[k]];}
    if(shared.size()==3)return true; // Duplicate faces are invalid independently.
    if(shared.size()==2){Vector3 otherA{},otherB{};for(auto p:pa)if(!allowed(p,shared))otherA=p;for(auto p:pb)if(!allowed(p,shared))otherB=p;if(orient3(pa[0],pa[1],pa[2],otherB)!=0)return false;return orient2(shared[0],shared[1],otherA,a.projection)==orient2(shared[0],shared[1],otherB,a.projection);}
    for(int k=0;k<3;++k)if(segmentBeyond(pa[k],pa[(k+1)%3],pb,b.projection,shared)||segmentBeyond(pb[k],pb[(k+1)%3],pa,a.projection,shared))return true;return false;
}
struct Node {Box box;std::size_t begin=0,end=0;int left=-1,right=-1;bool leaf()const{return left<0;}};
}
double signedMeshBoundaryVolume(const TriangleMesh& input){
    if(input.points.size()>100000||input.triangles.empty()||input.triangles.size()>200000||std::fegetround()!=FE_TONEAREST)throw std::runtime_error("Boundary volume input/rounding budget");
    for(const auto& p:input.points)for(double x:p)if(!std::isfinite(x)||std::abs(x)>1e9||(x!=0&&std::abs(x)<1e-40))throw std::runtime_error("Boundary volume numeric domain");
    Expansion total;const Vector3 origin{0,0,0};
    for(const auto& t:input.triangles)total=add(std::move(total),determinant3(input.points.at(t[0]),input.points.at(t[1]),input.points.at(t[2]),origin));
    double estimate=0;for(double x:total)estimate+=x;return estimate/6;
}
static Json auditMeshSpatialImpl(const TriangleMesh& input,const MeshSpatialBudget& budget,bool triangleLeaves){
    if(budget.trianglePairs>2000000||budget.nodePairs>8000000||budget.containmentTriangles>2000000||budget.componentPairs>4096||budget.issues<1||budget.issues>4096)throw std::runtime_error("Mesh spatial budget outside allowed range");
    Json issues=Json::array(),reasons=Json::array(),components=Json::array(),relations=Json::array();Json triangleComponents=nullptr;bool complete=true,stop=false;std::size_t nodeTests=0,pairTests=0,boxCandidates=0,nestingTests=0,componentTests=0,intersections=0,nested=0,skipped=0;
    auto incomplete=[&](const char* reason){complete=false;for(const auto& r:reasons)if(r==reason)return;reasons.push_back(reason);};
    if(input.points.size()>100000||input.triangles.size()>200000)incomplete("spatial_input_budget");
    if(std::fegetround()!=FE_TONEAREST)incomplete("round_to_nearest_required");
    if(complete)for(const auto& p:input.points)for(double x:p)if(!std::isfinite(x)||std::abs(x)>1e9||(x!=0&&std::abs(x)<1e-40))incomplete("unsupported_numeric_domain");
    if(input.triangles.empty())incomplete("empty_mesh");
    if(complete){
        auto topology=auditTriangleMesh(input);
        std::map<Vector3,std::size_t> unique;std::vector<Vector3> points;std::vector<std::size_t> remap;for(const auto& p:input.points){auto inserted=unique.emplace(p,points.size());if(inserted.second)points.push_back(p);remap.push_back(inserted.first->second);}
        std::vector<Triangle> triangles;triangles.reserve(input.triangles.size());Sets groups(input.triangles.size());std::map<std::pair<std::size_t,std::size_t>,std::size_t> edges;
        for(std::size_t f=0;f<input.triangles.size();++f){Face face=input.triangles[f];for(auto& v:face)v=remap.at(v);Triangle t;t.vertices=face;t.box.lo=t.box.hi=points[face[0]];for(auto v:face)for(int k=0;k<3;++k){t.box.lo[k]=std::min(t.box.lo[k],points[v][k]);t.box.hi[k]=std::max(t.box.hi[k],points[v][k]);}for(int drop=0;drop<3;++drop)if(orient2(points[face[0]],points[face[1]],points[face[2]],drop)!=0){t.projection=drop;break;}if(t.projection<0){++skipped;incomplete("degenerate_triangles_skipped");}triangles.push_back(t);for(int k=0;k<3;++k){auto key=std::minmax(face[k],face[(k+1)%3]);auto e=edges.emplace(key,f);if(!e.second)groups.join(f,e.first->second);}}
        std::map<std::size_t,std::vector<std::size_t>> grouped;for(std::size_t f=0;f<triangles.size();++f)grouped[groups.root(f)].push_back(f);std::vector<std::vector<std::size_t>> componentFaces;std::vector<Box> componentBoxes;std::vector<std::size_t> faceComponent(triangles.size());
        for(const auto& g:grouped){std::size_t id=componentFaces.size();Box box=triangles[g.second[0]].box;for(auto f:g.second){box=unite(box,triangles[f].box);faceComponent[f]=id;}componentFaces.push_back(g.second);componentBoxes.push_back(box);components.push_back({{"id",id},{"triangle_count",g.second.size()},{"representative_triangle_id",g.second.front()},{"bounds_mm",{{box.lo[0],box.hi[0]},{box.lo[1],box.hi[1]},{box.lo[2],box.hi[2]}}}});}
        triangleComponents=faceComponent;
        std::vector<std::size_t> order(triangles.size());std::iota(order.begin(),order.end(),0);std::vector<Node> nodes;nodes.reserve(2*triangles.size());
        std::function<int(std::size_t,std::size_t)> build=[&](std::size_t begin,std::size_t end){int n=int(nodes.size());Node node;node.begin=begin;node.end=end;node.box=triangles[order[begin]].box;for(auto i=begin+1;i<end;++i)node.box=unite(node.box,triangles[order[i]].box);nodes.push_back(node);if(end-begin>(triangleLeaves?1u:8u)){int axis=0;for(int k=1;k<3;++k)if(node.box.hi[k]-node.box.lo[k]>node.box.hi[axis]-node.box.lo[axis])axis=k;auto mid=begin+(end-begin)/2;std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](auto a,auto b){const auto& x=triangles[a].box;const auto& y=triangles[b].box;double ca=x.lo[axis]/2+x.hi[axis]/2,cb=y.lo[axis]/2+y.hi[axis]/2;return ca!=cb?ca<cb:a<b;});int l=build(begin,mid),r=build(mid,end);nodes[n].left=l;nodes[n].right=r;}return n;};
        auto emit=[&](Json issue){issues.push_back(std::move(issue));if(issues.size()>=budget.issues){stop=true;incomplete("issue_limit_reached");}};
        auto pair=[&](std::size_t a,std::size_t b){if(stop)return;if(pairTests>=budget.trianglePairs){stop=true;incomplete("triangle_pair_limit_reached");return;}++pairTests;const auto& x=triangles[a];const auto& y=triangles[b];if(!overlap(x.box,y.box))return;++boxCandidates;if(x.projection<0||y.projection<0)return;if(intersectsBeyond(x,y,points)){++intersections;emit({{"code","triangle_intersection_beyond_shared_simplex"},{"triangle_ids",{a,b}},{"component_ids",{faceComponent[a],faceComponent[b]}}});}};
        std::function<void(int,int)> scan=[&](int ia,int ib){if(stop)return;if(nodeTests>=budget.nodePairs){stop=true;incomplete("node_pair_limit_reached");return;}++nodeTests;const auto& a=nodes[ia];const auto& b=nodes[ib];if(!overlap(a.box,b.box))return;
            if(a.leaf()&&b.leaf()){for(auto i=a.begin;i<a.end&&!stop;++i)for(auto j=(ia==ib?i+1:b.begin);j<b.end&&!stop;++j)pair(order[i],order[j]);}
            else if(ia==ib){scan(a.left,a.left);scan(a.left,a.right);scan(a.right,a.right);}else if(!a.leaf()&&(b.leaf()||a.end-a.begin>=b.end-b.begin)){scan(a.left,ib);scan(a.right,ib);}else{scan(ia,b.left);scan(ia,b.right);}};
        int root=build(0,order.size());scan(root,root);
        if(complete&&intersections>0&&componentFaces.size()>1)incomplete("nesting_blocked_by_intersections");
        // After complete separation, one representative vertex suffices for a
        // connected closed component. Ambiguous ray-edge hits retry directions.
        if(complete&&intersections==0&&componentFaces.size()>1){
            if(topology.at("closed_oriented_surface")!=true)incomplete("nesting_requires_closed_oriented_components");
            else{
                Box global=nodes[0].box;double span=0;for(int k=0;k<3;++k)span=std::max(span,global.hi[k]-global.lo[k]);
                auto inside=[&](std::size_t from,std::size_t target)->int{const auto& point=points[triangles[componentFaces[from][0]].vertices[0]];if(!contains(componentBoxes[target],point))return 0;
                    const std::array<Vector3,7> directions{{{1,2,3},{2,3,5},{3,5,7},{5,7,11},{7,11,13},{11,13,17},{13,17,19}}};
                    for(auto direction:directions){Vector3 end;for(int k=0;k<3;++k)end[k]=point[k]+direction[k]*span*8;std::size_t hits=0;bool ambiguous=false;
                        for(auto f:componentFaces[target]){if(nestingTests>=budget.containmentTriangles){incomplete("containment_triangle_limit_reached");return -1;}++nestingTests;const auto& face=triangles[f];const auto& t=face.vertices;const auto& a=points[t[0]];const auto& b=points[t[1]];const auto& c=points[t[2]];int p=orient3(a,b,c,point),q=orient3(a,b,c,end);if(p*q>0)continue;if(p==0&&q==0){ambiguous=true;break;}int e1=orient3(point,end,a,b),e2=orient3(point,end,b,c),e3=orient3(point,end,c,a);if(!allSame(e1,e2,e3))continue;if(p==0||q==0||e1==0||e2==0||e3==0){ambiguous=true;break;}++hits;}
                        if(!ambiguous)return int(hits%2);
                    }incomplete("containment_ray_degeneracy");return -1;
                };
                for(std::size_t a=0;a<componentFaces.size()&&complete&&!stop;++a)for(std::size_t b=a+1;b<componentFaces.size()&&complete&&!stop;++b){if(componentTests>=budget.componentPairs){incomplete("component_pair_limit_reached");break;}++componentTests;int aInB=inside(a,b),bInA=complete?inside(b,a):-1;if(aInB<0||bInA<0)break;if(aInB&&bInA){incomplete("contradictory_component_containment");break;}if(aInB||bInA){++nested;auto inner=aInB?a:b,outer=aInB?b:a;relations.push_back({{"inside_component",inner},{"outside_component",outer}});emit({{"code","nested_components"},{"component_ids",{inner,outer}},{"triangle_ids",{componentFaces[inner].front(),componentFaces[outer].front()}}});}}
            }
        }
    }
    Json report={{"schema",triangleLeaves?"scdat.radiation-mesh-spatial/2":"scdat.radiation-mesh-spatial/1"},{"status",!issues.empty()?"issues_found":complete?"clear":"incomplete"},{"complete",complete},{"incomplete_reasons",reasons},{"issues",issues},{"intersection_pair_count",intersections},{"nested_component_pair_count",nested},{"nesting_relations",relations},{"nesting_status",components.empty()?"not_evaluated":components.size()==1?"not_applicable_single_component":intersections>0?"blocked_by_intersections":complete?"evaluated":"incomplete"},{"components",components},{"triangle_component_ids",triangleComponents},{"skipped_degenerate_triangles",skipped},
        {"triangle_pair_tests",pairTests},{"aabb_candidate_pairs",boxCandidates},{"node_pair_visits",nodeTests},{"containment_triangle_tests",nestingTests},{"component_pair_tests",componentTests},
        {"budget",{{"triangle_pairs",budget.trianglePairs},{"node_pairs",budget.nodePairs},{"containment_triangles",budget.containmentTriangles},{"component_pairs",budget.componentPairs},{"issues",budget.issues}}},
        {"predicate","binary64 expansion TwoSum/FMA TwoProduct; exact signs within declared arithmetic domain"},{"numeric_domain",{{"max_abs_coordinate_mm",1e9},{"min_nonzero_abs_coordinate_mm",1e-40},{"rounding","FE_TONEAREST"}}},
        {"contact_policy","exact-coordinate shared vertex/edge allowed only; intersection beyond shared simplex is an issue; no tolerance snapping"},{"triangle_id_base",0},{"unchecked",{"cad_source_units_and_fidelity","material_cavity_interpretation","transport_geometry_validity"}}};
    if(triangleLeaves){report["algorithm"]="single_triangle_leaf_bvh/1";report["leaf_triangle_capacity"]=1;report["pair_budget_unit"]="overlapping_triangle_leaf_pair";report["node_budget_unit"]="visited_node_pair_before_aabb_test";}return report;
}
Json auditMeshSpatial(const TriangleMesh& input,const MeshSpatialBudget& budget){return auditMeshSpatialImpl(input,budget,false);}
Json auditMeshSpatialLeafBvh(const TriangleMesh& input,const MeshSpatialBudget& budget){return auditMeshSpatialImpl(input,budget,true);}
static Json auditMeshAssemblyImpl(const std::vector<MeshAssemblyPart>& input,const MeshAssemblyBudget& budget,bool triangleLeaves){
    if(input.empty()||input.size()>32)throw std::runtime_error("Assembly requires 1..32 mesh parts");
    if(budget.trianglePairs>2000000||budget.nodePairs>8000000||budget.containmentTriangles>2000000||budget.partPairs>4096||budget.issues<1||budget.issues>4096)throw std::runtime_error("Assembly budget outside allowed range");
    Json reasons=Json::array(),partRows=Json::array(),pairs=Json::array(),issues=Json::array();bool complete=true,limited=false;std::size_t pairTests=0,nodeTests=0,containmentTests=0,partTests=0;
    auto incomplete=[&](const char* reason){complete=false;if(std::find(reasons.begin(),reasons.end(),reason)==reasons.end())reasons.push_back(reason);};
    std::set<std::string> ids;std::size_t pointCount=0,faceCount=0;
    if(std::fegetround()!=FE_TONEAREST)incomplete("round_to_nearest_required");
    for(const auto& part:input){if(part.id.empty()||part.id.size()>256||!ids.insert(part.id).second)throw std::runtime_error("Assembly part IDs must be unique/nonempty");pointCount+=part.mesh.points.size();faceCount+=part.mesh.triangles.size();if(part.mesh.triangles.empty())incomplete("empty_part_mesh");for(const auto& p:part.mesh.points)for(double x:p)if(!std::isfinite(x)||std::abs(x)>1e9||(x!=0&&std::abs(x)<1e-40))incomplete("unsupported_numeric_domain");for(auto face:part.mesh.triangles)for(auto v:face)if(v>=part.mesh.points.size())throw std::runtime_error("Assembly face index outside part points");}
    if(pointCount>200000||faceCount>200000)incomplete("assembly_input_budget");
    struct Body {std::vector<Triangle> triangles;std::vector<std::size_t> order;std::vector<Node> nodes;bool qualified=false;};std::vector<Body> bodies;std::vector<Vector3> points;Box global{};
    if(complete){
        for(const auto& part:input){Body body;auto quality=auditTriangleMesh(part.mesh),spatial=triangleLeaves?auditMeshSpatialLeafBvh(part.mesh):auditMeshSpatial(part.mesh);Json blockers=Json::array();if(quality.at("closed_oriented_surface")!=true)blockers.push_back("surface_not_closed_or_consistently_oriented");if(quality.at("edge_connected_component_count")!=1)blockers.push_back("single_component_required");if(quality.at("unused_welded_point_count")!=0)blockers.push_back("unused_vertices");if(quality.at("signed_volume_mm3").is_null()||quality.at("signed_volume_mm3").get<double>()<=0)blockers.push_back("positive_signed_volume_required");if(spatial.at("complete")!=true||!spatial.at("issues").empty())blockers.push_back("self_spatial_audit_not_clear");body.qualified=blockers.empty();if(!body.qualified)incomplete("one_or_more_parts_unqualified");
            partRows.push_back({{"id",part.id},{"triangle_count",part.mesh.triangles.size()},{"qualified_single_solid",body.qualified},{"blockers",blockers},{"self_spatial_complete",spatial.at("complete")}});
            auto offset=points.size();points.insert(points.end(),part.mesh.points.begin(),part.mesh.points.end());for(auto face:part.mesh.triangles){for(auto& v:face)v+=offset;Triangle t;t.vertices=face;t.box.lo=t.box.hi=points[face[0]];for(auto v:face)for(int k=0;k<3;++k){t.box.lo[k]=std::min(t.box.lo[k],points[v][k]);t.box.hi[k]=std::max(t.box.hi[k],points[v][k]);}for(int drop=0;drop<3;++drop)if(orient2(points[face[0]],points[face[1]],points[face[2]],drop)!=0){t.projection=drop;break;}if(t.projection<0)incomplete("degenerate_part_triangles");body.triangles.push_back(t);}
            body.order.resize(body.triangles.size());std::iota(body.order.begin(),body.order.end(),0);
            std::function<int(std::size_t,std::size_t)> build=[&](auto begin,auto end){int n=int(body.nodes.size());Node node;node.begin=begin;node.end=end;node.box=body.triangles[body.order[begin]].box;for(auto i=begin+1;i<end;++i)node.box=unite(node.box,body.triangles[body.order[i]].box);body.nodes.push_back(node);if(end-begin>8){int axis=0;for(int k=1;k<3;++k)if(node.box.hi[k]-node.box.lo[k]>node.box.hi[axis]-node.box.lo[axis])axis=k;auto mid=begin+(end-begin)/2;std::nth_element(body.order.begin()+begin,body.order.begin()+mid,body.order.begin()+end,[&](auto a,auto b){const auto& x=body.triangles[a].box;const auto& y=body.triangles[b].box;double ca=x.lo[axis]/2+x.hi[axis]/2,cb=y.lo[axis]/2+y.hi[axis]/2;return ca!=cb?ca<cb:a<b;});int l=build(begin,mid),r=build(mid,end);body.nodes[n].left=l;body.nodes[n].right=r;}return n;};build(0,body.order.size());global=bodies.empty()?body.nodes[0].box:unite(global,body.nodes[0].box);bodies.push_back(std::move(body));
        }
        double span=0;for(int k=0;k<3;++k)span=std::max(span,global.hi[k]-global.lo[k]);
        auto inside=[&](std::size_t from,std::size_t target)->int{const auto& point=points[bodies[from].triangles[0].vertices[0]];const auto& body=bodies[target];if(!contains(body.nodes[0].box,point))return 0;const std::array<Vector3,7> directions{{{1,2,3},{2,3,5},{3,5,7},{5,7,11},{7,11,13},{11,13,17},{13,17,19}}};for(auto direction:directions){Vector3 end;for(int k=0;k<3;++k)end[k]=point[k]+direction[k]*span*8;std::size_t hits=0;bool ambiguous=false;for(const auto& face:body.triangles){if(containmentTests>=budget.containmentTriangles){incomplete("containment_triangle_limit_reached");limited=true;return -1;}++containmentTests;const auto& t=face.vertices;const auto& a=points[t[0]];const auto& b=points[t[1]];const auto& c=points[t[2]];int p=orient3(a,b,c,point),q=orient3(a,b,c,end);if(p*q>0)continue;if(p==0&&q==0){ambiguous=true;break;}int e1=orient3(point,end,a,b),e2=orient3(point,end,b,c),e3=orient3(point,end,c,a);if(!allSame(e1,e2,e3))continue;if(p==0||q==0||e1==0||e2==0||e3==0){ambiguous=true;break;}++hits;}if(!ambiguous)return int(hits%2);}incomplete("containment_ray_degeneracy");return -1;};
        for(std::size_t a=0;a<bodies.size();++a)for(std::size_t b=a+1;b<bodies.size();++b){Json row={{"part_ids",{input[a].id,input[b].id}},{"triangle_ids",Json::array()},{"classification","incomplete"},{"classification_complete",false},{"basis","not_evaluated"}};
            if(limited||partTests>=budget.partPairs){limited=true;incomplete("part_pair_limit_or_global_budget");pairs.push_back(row);continue;}++partTests;
            const auto& left=bodies[a];const auto& right=bodies[b];bool witness=false,pairComplete=true;std::size_t faceA=0,faceB=0;
            bool boxesOverlap=overlap(left.nodes[0].box,right.nodes[0].box);
            std::function<void(int,int)> scan=[&](int ia,int ib){if(witness||limited)return;if(nodeTests>=budget.nodePairs){incomplete("node_pair_limit_reached");limited=true;pairComplete=false;return;}++nodeTests;const auto& na=left.nodes[ia];const auto& nb=right.nodes[ib];if(!overlap(na.box,nb.box))return;if(na.leaf()&&nb.leaf()){for(auto i=na.begin;i<na.end&&!witness&&!limited;++i)for(auto j=nb.begin;j<nb.end&&!witness&&!limited;++j){if(pairTests>=budget.trianglePairs){incomplete("triangle_pair_limit_reached");limited=true;pairComplete=false;break;}++pairTests;auto x=left.order[i],y=right.order[j];const auto& ta=left.triangles[x];const auto& tb=right.triangles[y];if(!overlap(ta.box,tb.box))continue;if(ta.projection<0||tb.projection<0){incomplete("degenerate_pair_triangle");pairComplete=false;continue;}if(intersectsBeyond(ta,tb,points)){witness=true;faceA=x;faceB=y;}}}else if(!na.leaf()&&(nb.leaf()||na.end-na.begin>=nb.end-nb.begin)){scan(na.left,ib);scan(na.right,ib);}else{scan(ia,nb.left);scan(ia,nb.right);}};
            if(boxesOverlap)scan(0,0);row["basis"]=boxesOverlap?"bvh_boundary_scan":"strict_aabb_separation";
            if(witness){row["classification"]="contact_or_intersection";row["classification_complete"]=true;row["basis"]="first_exact_boundary_witness";row["triangle_ids"]={faceA,faceB};issues.push_back({{"code","cross_part_boundary_contact_or_intersection"},{"part_ids",row.at("part_ids")},{"triangle_ids",row.at("triangle_ids")}});}
            else if(pairComplete&&!limited){if(!left.qualified||!right.qualified){row["classification"]="unqualified_parts";incomplete("pair_containment_requires_qualified_parts");}else{int aInB=boxesOverlap?inside(a,b):0,bInA=boxesOverlap&&!limited?inside(b,a):0;if(aInB<0||bInA<0||limited)row["classification"]="incomplete";else if(aInB&&bInA)incomplete("contradictory_part_containment");else{row["classification_complete"]=true;if(aInB||bInA){row["classification"]=aInB?"a_inside_b":"b_inside_a";row["basis"]="separated_boundaries_and_exact_parity";row["triangle_ids"]={0,0};issues.push_back({{"code","nested_filled_parts"},{"part_ids",row.at("part_ids")},{"triangle_ids",row.at("triangle_ids")},{"inside_part",input[aInB?a:b].id},{"outside_part",input[aInB?b:a].id}});}else row["classification"]="separated";}}}
            pairs.push_back(row);if(issues.size()>=budget.issues){incomplete("issue_limit_reached");limited=true;}
        }
    }
    bool passed=complete&&issues.empty();Json report={{"schema",triangleLeaves?"scdat.mesh-assembly-relations/2":"scdat.mesh-assembly-relations/1"},{"complete",complete},{"review_gate_passed",passed},{"status",!issues.empty()?"issues_found":complete?"clear":"incomplete"},{"incomplete_reasons",reasons},{"parts",partRows},{"pairs",pairs},{"issues",issues},{"possible_part_pairs",input.size()*(input.size()-1)/2},{"part_pair_tests",partTests},{"triangle_pair_tests",pairTests},{"node_pair_visits",nodeTests},{"containment_triangle_tests",containmentTests},{"budget",{{"triangle_pairs",budget.trianglePairs},{"node_pairs",budget.nodePairs},{"containment_triangles",budget.containmentTriangles},{"part_pairs",budget.partPairs},{"issues",budget.issues}}},{"self_audit_budget","fixed default per part; separately bounded, at most 32 parts"},{"coordinates","world_mm"},{"triangle_id_base",0},{"contact_policy","distinct parts share no simplex; all exact boundary contact is unresolved; no snapping"},{"all_contact_faces_enumerated",false},{"transport_allowed",false},{"scope","first boundary witness per pair or strict separation/qualified single-solid containment; no BRep, assembly transform fidelity, overlap volume or transport certificate"}};
    if(triangleLeaves){report["self_spatial_algorithm"]="single_triangle_leaf_bvh/1";report["cross_part_algorithm"]="legacy_eight_triangle_leaf_bvh/1";}return report;
}
Json auditMeshAssembly(const std::vector<MeshAssemblyPart>& input,const MeshAssemblyBudget& budget){return auditMeshAssemblyImpl(input,budget,false);}
Json auditMeshAssemblyLeafBvh(const std::vector<MeshAssemblyPart>& input,const MeshAssemblyBudget& budget){return auditMeshAssemblyImpl(input,budget,true);}

Json explainMeshAssemblyContacts(const std::vector<MeshAssemblyPart>& input,const MeshContactBudget& budget){
    if(budget.supportVertices>2000000||budget.pointTriangles>8000000||budget.identityTriangles>2000000||budget.boundaryTriangles>2000000||budget.boundaryNodes>8000000)throw std::runtime_error("Contact evidence budget outside allowed range");
    auto base=auditMeshAssemblyLeafBvh(input);Json rows=Json::array(),limits=Json::array();std::size_t supportWork=0,pointWork=0,identityWork=0,faceWork=0,nodeWork=0,positive=0,contact=0,unresolved=0,separated=0;
    struct Body {std::vector<Triangle> ts;std::vector<std::size_t> order;std::vector<Node> nodes;bool qualified=false;};std::vector<Body> bodies(input.size());
    if(base.at("parts").size()==input.size())for(std::size_t b=0;b<input.size();++b){auto& body=bodies[b];body.qualified=base.at("parts")[b].at("qualified_single_solid");if(!body.qualified)continue;const auto& mesh=input[b].mesh;
        for(const auto& face:mesh.triangles){Triangle t;t.vertices=face;t.box.lo=t.box.hi=mesh.points[face[0]];for(auto v:face)for(int k=0;k<3;++k){t.box.lo[k]=std::min(t.box.lo[k],mesh.points[v][k]);t.box.hi[k]=std::max(t.box.hi[k],mesh.points[v][k]);}for(int drop=0;drop<3;++drop)if(orient2(mesh.points[face[0]],mesh.points[face[1]],mesh.points[face[2]],drop)!=0){t.projection=drop;break;}body.ts.push_back(t);}
        body.order.resize(body.ts.size());std::iota(body.order.begin(),body.order.end(),0);body.nodes.reserve(2*body.ts.size());
        std::function<int(std::size_t,std::size_t)> build=[&](auto begin,auto end){int id=int(body.nodes.size());Node n;n.begin=begin;n.end=end;n.box=body.ts[body.order[begin]].box;for(auto i=begin+1;i<end;++i)n.box=unite(n.box,body.ts[body.order[i]].box);body.nodes.push_back(n);if(end-begin>1){int axis=0;for(int k=1;k<3;++k)if(n.box.hi[k]-n.box.lo[k]>n.box.hi[axis]-n.box.lo[axis])axis=k;auto mid=begin+(end-begin)/2;std::nth_element(body.order.begin()+begin,body.order.begin()+mid,body.order.begin()+end,[&](auto a,auto b){auto x=body.ts[a].box,y=body.ts[b].box;double ca=x.lo[axis]/2+x.hi[axis]/2,cb=y.lo[axis]/2+y.hi[axis]/2;return ca!=cb?ca<cb:a<b;});int l=build(begin,mid),r=build(mid,end);body.nodes[id].left=l;body.nodes[id].right=r;}return id;};build(0,body.order.size());
    }
    auto limited=[&](Json& row,const char* reason){auto& rs=row["limits_reached"];if(std::find(rs.begin(),rs.end(),reason)==rs.end())rs.push_back(reason);if(std::find(limits.begin(),limits.end(),reason)==limits.end())limits.push_back(reason);};
    std::map<std::string,std::size_t> index;for(std::size_t i=0;i<input.size();++i)index[input[i].id]=i;
    for(const auto& original:base.at("pairs")){Json row={{"part_ids",original.at("part_ids")},{"base_classification",original.at("classification")},{"solid_relation","unresolved"},{"classification_complete",false},{"proof",nullptr},{"limits_reached",Json::array()}};auto a=index.at(original.at("part_ids")[0].get<std::string>()),b=index.at(original.at("part_ids")[1].get<std::string>());const auto& A=input[a].mesh;const auto& B=input[b].mesh;const auto& ba=bodies[a];const auto& bb=bodies[b];
        auto resolve=[&](const char* status,Json proof){row["solid_relation"]=status;row["classification_complete"]=true;row["proof"]=std::move(proof);};auto kind=original.at("classification").get<std::string>();
        if(kind=="separated"&&original.at("classification_complete")==true){resolve("separated",{{"kind","existing_complete_separation"},{"basis",original.at("basis")}});++separated;rows.push_back(row);continue;}
        if(!ba.qualified||!bb.qualified||original.at("classification_complete")!=true){row["solid_relation"]="not_evaluated";++unresolved;rows.push_back(row);continue;}
        if(kind=="a_inside_b"||kind=="b_inside_a"){resolve("positive_overlap_proven",{{"kind","existing_strict_filled_containment"},{"inside_part",input[kind=="a_inside_b"?a:b].id},{"triangle_ids",original.at("triangle_ids")}});++positive;rows.push_back(row);continue;}
        if(kind!="contact_or_intersection"){++unresolved;rows.push_back(row);continue;}
        const auto& boxA=ba.nodes[0].box;const auto& boxB=bb.nodes[0].box;bool done=false;
        for(int k=0;k<3&&!done;++k){bool forward=boxA.hi[k]==boxB.lo[k],backward=boxB.hi[k]==boxA.lo[k];if(forward||backward){resolve("contact_only_proven",{{"kind","axis_supporting_plane"},{"axis",k},{"coordinate_mm",forward?boxA.hi[k]:boxB.hi[k]},{"a_closed_halfspace",forward?"le":"ge"},{"triangle_ids",original.at("triangle_ids")}});done=true;}}
        std::vector<std::pair<std::size_t,std::size_t>> planes;auto addPlane=[&](std::size_t part,std::size_t face){auto pair=std::make_pair(part,face);if(std::find(planes.begin(),planes.end(),pair)==planes.end())planes.push_back(pair);};addPlane(a,original.at("triangle_ids")[0].get<std::size_t>());addPlane(b,original.at("triangle_ids")[1].get<std::size_t>());for(auto part:{a,b})for(std::size_t i=0;i<7;++i)addPlane(part,i*(input[part].mesh.triangles.size()-1)/6);
        auto supportPlane=[&](const std::pair<std::size_t,std::size_t>& candidate){auto bodyIndex=candidate.first;auto faceId=candidate.second;const auto& mesh=input[bodyIndex].mesh;const auto& f=mesh.triangles[faceId];std::array<int,2> side{};bool valid=true;
            for(int target=0;target<2&&valid;++target)for(const auto& p:input[target==0?a:b].mesh.points){if(supportWork>=budget.supportVertices){limited(row,"support_vertex_limit_reached");valid=false;break;}++supportWork;int sign=orient3(mesh.points[f[0]],mesh.points[f[1]],mesh.points[f[2]],p);if(sign){if(side[target]&&side[target]!=sign){valid=false;break;}side[target]=sign;}}
            if(valid&&side[0]!=0&&side[0]==-side[1]){resolve("contact_only_proven",{{"kind","triangle_supporting_plane"},{"plane_part",input[bodyIndex].id},{"plane_triangle_id",faceId},{"a_nonzero_side",side[0]},{"b_nonzero_side",side[1]},{"triangle_ids",bodyIndex==a?Json::array({faceId,original.at("triangle_ids")[1]}):Json::array({original.at("triangle_ids")[0],faceId})}});done=true;}
        };
        for(std::size_t i=0;i<std::min<std::size_t>(2,planes.size())&&!done;++i)supportPlane(planes[i]);
        // A rounded midpoint is a query point, not a constructed intersection.
        // Its actual binary64 position must be strictly inside both solids.
        Vector3 query{};bool queryValid=true;for(int k=0;k<3;++k){double lo=std::max(boxA.lo[k],boxB.lo[k]),hi=std::min(boxA.hi[k],boxB.hi[k]);query[k]=lo/2+hi/2;if(!(lo<hi&&query[k]>lo&&query[k]<hi)||!std::isfinite(query[k])||std::abs(query[k])>1e9||(query[k]!=0&&std::abs(query[k])<1e-40))queryValid=false;}
        auto inside=[&](std::size_t bodyIndex,Json& certificate)->int{const auto& mesh=input[bodyIndex].mesh;const auto& body=bodies[bodyIndex];
            for(const auto& t:body.ts){if(pointWork>=budget.pointTriangles){limited(row,"point_triangle_limit_reached");return -1;}++pointWork;const auto& f=t.vertices;if(orient3(mesh.points[f[0]],mesh.points[f[1]],mesh.points[f[2]],query)==0&&inTriangle(query,{mesh.points[f[0]],mesh.points[f[1]],mesh.points[f[2]]},t.projection))return 0;}
            double span=0;for(int k=0;k<3;++k)span=std::max(span,body.nodes[0].box.hi[k]-body.nodes[0].box.lo[k]);const std::array<Vector3,7> directions{{{1,2,3},{2,3,5},{3,5,7},{5,7,11},{7,11,13},{11,13,17},{13,17,19}}};
            for(std::size_t d=0;d<directions.size();++d){Vector3 end;for(int k=0;k<3;++k)end[k]=query[k]+directions[d][k]*span*8;bool ambiguous=false;std::size_t hits=0;
                for(const auto& t:body.ts){if(pointWork>=budget.pointTriangles){limited(row,"point_triangle_limit_reached");return -1;}++pointWork;const auto& f=t.vertices;const auto& p=mesh.points[f[0]];const auto& q=mesh.points[f[1]];const auto& r=mesh.points[f[2]];int s=orient3(p,q,r,query),e=orient3(p,q,r,end);if(s*e>0)continue;if(s==0&&e==0){ambiguous=true;break;}int x=orient3(query,end,p,q),y=orient3(query,end,q,r),z=orient3(query,end,r,p);if(!allSame(x,y,z))continue;if(s==0||e==0||x==0||y==0||z==0){ambiguous=true;break;}++hits;}
                if(!ambiguous){certificate={{"part_id",input[bodyIndex].id},{"ray_direction_index",d},{"ray_end_mm",end},{"strict_crossing_count",hits}};return int(hits%2);}
            }return -1;
        };
        if(!done&&queryValid){Json ca,cb;int inA=inside(a,ca),inB=inA==1?inside(b,cb):0;if(inA==1&&inB==1){resolve("positive_overlap_proven",{{"kind","common_strict_interior_point"},{"point_mm",query},{"point_location",Json::array({ca,cb})}});done=true;}}
        if(!done&&boxA.lo==boxB.lo&&boxA.hi==boxB.hi&&A.triangles.size()==B.triangles.size()){
            if(identityWork+A.triangles.size()+B.triangles.size()>budget.identityTriangles)limited(row,"identity_triangle_limit_reached");else{using Key=std::array<Vector3,3>;auto keys=[&](const TriangleMesh& m){std::vector<Key> out;for(const auto& f:m.triangles){Key k{m.points[f[0]],m.points[f[1]],m.points[f[2]]};std::sort(k.begin(),k.end());out.push_back(k);}std::sort(out.begin(),out.end());return out;};identityWork+=A.triangles.size()+B.triangles.size();if(keys(A)==keys(B)){resolve("positive_overlap_proven",{{"kind","identical_qualified_closed_boundary"},{"triangle_count_each",A.triangles.size()},{"triangle_ids",original.at("triangle_ids")}});done=true;}}
        }
        auto edgeCross=[&](std::size_t source,std::size_t target,std::size_t sf,std::size_t tf,int sourceSlot)->bool{const auto& sm=input[source].mesh;const auto& tm=input[target].mesh;const auto& s=sm.triangles[sf];const auto& t=tm.triangles[tf];const auto& p=tm.points[t[0]];const auto& q=tm.points[t[1]];const auto& r=tm.points[t[2]];
            for(int k=0;k<3;++k){const auto& u=sm.points[s[k]];const auto& v=sm.points[s[(k+1)%3]];int su=orient3(p,q,r,u),sv=orient3(p,q,r,v);if(su*sv>=0)continue;std::array<int,3> es{orient3(u,v,p,q),orient3(u,v,q,r),orient3(u,v,r,p)};if((es[0]>0&&es[1]>0&&es[2]>0)||(es[0]<0&&es[1]<0&&es[2]<0)){Json faces=sourceSlot==0?Json::array({sf,tf}):Json::array({tf,sf});resolve("positive_overlap_proven",{{"kind","strict_edge_through_face_interior"},{"triangle_ids",faces},{"edge_part",input[source].id},{"edge_vertex_indices",{s[k],s[(k+1)%3]}},{"face_part",input[target].id},{"target_triangle_id",tf},{"endpoint_plane_signs",{su,sv}},{"edge_triangle_signs",es}});return true;}}return false;
        };
        bool scanLimited=false;std::function<void(int,int)> scan=[&](int ia,int ib){if(done||scanLimited)return;if(nodeWork>=budget.boundaryNodes){limited(row,"boundary_node_limit_reached");scanLimited=true;return;}++nodeWork;const auto& x=ba.nodes[ia];const auto& y=bb.nodes[ib];if(!overlap(x.box,y.box))return;if(x.leaf()&&y.leaf()){if(faceWork>=budget.boundaryTriangles){limited(row,"boundary_triangle_limit_reached");scanLimited=true;return;}++faceWork;auto fa=ba.order[x.begin],fb=bb.order[y.begin];done=edgeCross(a,b,fa,fb,0)||edgeCross(b,a,fb,fa,1);}else if(!x.leaf()&&(y.leaf()||x.end-x.begin>=y.end-y.begin)){scan(x.left,ib);scan(x.right,ib);}else{scan(ia,y.left);scan(ia,y.right);}};
        if(!done)scan(0,0);
        for(std::size_t i=2;i<planes.size()&&!done;++i)supportPlane(planes[i]);
        if(row.at("solid_relation")=="positive_overlap_proven")++positive;else if(row.at("solid_relation")=="contact_only_proven")++contact;else ++unresolved;rows.push_back(row);
    }
    unresolved+=base.at("possible_part_pairs").get<std::size_t>()-rows.size();
    bool evidenceComplete=base.at("complete")==true&&rows.size()==base.at("possible_part_pairs").get<std::size_t>()&&unresolved==0;
    return {{"schema","scdat.mesh-contact-evidence/1"},{"support_plane_candidates_per_pair",16},{"base_relations",base},{"pairs",rows},{"evidence_complete",evidenceComplete},{"separated_pair_count",separated},{"positive_overlap_pair_count",positive},{"contact_only_pair_count",contact},{"unresolved_pair_count",unresolved},{"limits_reached",limits},{"work",{{"support_vertices",supportWork},{"point_triangles",pointWork},{"identity_triangles",identityWork},{"boundary_triangles",faceWork},{"boundary_nodes",nodeWork}}},{"budget",{{"support_vertices",budget.supportVertices},{"point_triangles",budget.pointTriangles},{"identity_triangles",budget.identityTriangles},{"boundary_triangles",budget.boundaryTriangles},{"boundary_nodes",budget.boundaryNodes}}},{"review_gate_passed",base.at("review_gate_passed")},{"transport_allowed",false},{"scope","Sufficient exact discrete-mesh proofs only; unresolved is not separation. No CAD tolerance, overlap volume, repair or contact transport approval"}};
}

}
