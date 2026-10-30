#include "ConvexSeparation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <cfenv>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif
namespace spacerad::engine {
namespace {
static_assert(std::numeric_limits<double>::is_iec559&&sizeof(double)==8,"IEEE binary64 required");
// This scope changes only the calling thread and restores its exact FP state.
// It prevents caller-selected flush-to-zero or directed rounding from invalidating
// nextafter enclosures. Physical transport runs after this scope has ended.
struct FloatingScope {
#if defined(_M_X64) || defined(__SSE2__)
    unsigned int saved=_mm_getcsr();
    FloatingScope(){_mm_setcsr(saved&~0xe040u);}
    ~FloatingScope(){_mm_setcsr(saved);}
#else
    FloatingScope(){if(std::fegetround()!=FE_TONEAREST)throw std::runtime_error("Convex interval arithmetic requires nearest rounding");}
#endif
};
struct Interval {double lo,hi;};
double lower(double x){
    if(!std::isfinite(x))throw std::runtime_error("Convex separation: nonfinite interval operation");
    // A normal-sized enclosure also covers underflow/flush-to-zero results.
    if(std::abs(x)<std::numeric_limits<double>::min())return -std::numeric_limits<double>::min();
    return std::nextafter(x,-std::numeric_limits<double>::infinity());
}
double upper(double x){if(!std::isfinite(x))throw std::runtime_error("Convex separation: nonfinite interval operation");if(std::abs(x)<std::numeric_limits<double>::min())return std::numeric_limits<double>::min();return std::nextafter(x,std::numeric_limits<double>::infinity());}
Interval exact(double x){if(!std::isfinite(x))throw std::runtime_error("Convex separation: nonfinite input");return {x,x};}
Interval add(Interval a,Interval b){return {lower(a.lo+b.lo),upper(a.hi+b.hi)};}
Interval multiply(Interval a,Interval b){double v[4]={a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};return {lower(*std::min_element(v,v+4)),upper(*std::max_element(v,v+4))};}
using PointInterval=std::array<Interval,3>;
PointInterval world(const TriangleMeshReview& p,const Vector3& local){PointInterval out;for(int i=0;i<3;++i){out[i]=exact(p.placement.translation[i]);for(int j=0;j<3;++j)out[i]=add(out[i],multiply(exact(p.placement.rotation[i][j]),exact(local[j])));}return out;}
Vector3 subtract(Vector3 a,Vector3 b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector3 cross(Vector3 a,Vector3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
struct Part {
    std::string id;std::vector<PointInterval> points;std::vector<Vector3> nominal;
    std::vector<std::array<size_t,3>> triangles;std::vector<std::array<size_t,2>> edges;
};
Part prepare(const TriangleMeshReview& review){
    Part p;p.id=review.report.at("id");p.triangles=review.mesh.triangles;
    for(auto v:review.mesh.points){p.points.push_back(world(review,v));p.nominal.push_back(review.placement.apply(v));}
    std::set<std::array<size_t,2>> edges;for(auto t:p.triangles)for(int k=0;k<3;++k){auto a=t[k],b=t[(k+1)%3];edges.insert({std::min(a,b),std::max(a,b)});}p.edges.assign(edges.begin(),edges.end());return p;
}
Interval projection(const Part& p,const Vector3& axis){Interval range{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};for(const auto& point:p.points){auto v=exact(0);for(int k=0;k<3;++k)v=add(v,multiply(point[k],exact(axis[k])));range.lo=std::min(range.lo,v.lo);range.hi=std::max(range.hi,v.hi);}return range;}
Json pair(const Part& a,const Part& b,const ConvexSeparationBudget& budget,size_t& work){
    size_t axes=0,candidates=0;bool stop=false,exhausted=false;std::set<Vector3> seen;Json witness=nullptr;
    auto attempt=[&](Vector3 axis,const char* family){
        if(stop)return false;
        if(candidates>=budget.candidatesPerPair){stop=exhausted=true;return false;}++candidates;
        double scale=std::max({std::abs(axis[0]),std::abs(axis[1]),std::abs(axis[2])});if(!std::isfinite(scale)||scale<std::numeric_limits<double>::min())return false;
        for(auto& x:axis){x/=scale;if(x==0)x=0;}for(auto x:axis)if(x!=0){if(x<0)for(auto& y:axis)y=-y;break;}
        if(!seen.insert(axis).second)return false;
        const size_t needed=a.points.size()+b.points.size();
        if(axes>=budget.axesPerPair||work>budget.vertexProjections||needed>budget.vertexProjections-work){stop=exhausted=true;return false;}
        ++axes;work+=needed;auto pa=projection(a,axis),pb=projection(b,axis);double ab=lower(pb.lo-pa.hi),ba=lower(pa.lo-pb.hi);bool forward=ab>=ba;double gap=std::max(ab,ba);
        auto norm=exact(0);for(auto x:axis)norm=add(norm,exact(std::abs(x)));double distance=gap>0?lower(gap/norm.hi):gap;
        if(distance<=1e-4)return false;
        witness={{"axis_world",axis},{"axis_norm_upper_L1",norm.hi},{"a_projection_interval",{pa.lo,pa.hi}},{"b_projection_interval",{pb.lo,pb.hi}},{"order",forward?"a_before_b":"b_before_a"},{"projection_gap_lower_bound",gap},{"gap_lower_bound_mm",distance},{"family",family}};stop=true;return true;
    };
    attempt({1,0,0},"world_axis");attempt({0,1,0},"world_axis");attempt({0,0,1},"world_axis");
    for(const auto* p:{&a,&b}){for(auto t:p->triangles){if(stop)break;attempt(cross(subtract(p->nominal[t[1]],p->nominal[t[0]]),subtract(p->nominal[t[2]],p->nominal[t[0]])),p==&a?"face_a":"face_b");}if(stop)break;}
    for(auto ea:a.edges){if(stop)break;for(auto eb:b.edges){if(stop)break;attempt(cross(subtract(a.nominal[ea[1]],a.nominal[ea[0]]),subtract(b.nominal[eb[1]],b.nominal[eb[0]])),"edge_cross");}}
    return {{"a",a.id},{"b",b.id},{"status",!witness.is_null()?"separated":exhausted?"budget_exhausted":"no_strict_separation_witness"},{"complete",!exhausted},{"axes_evaluated",axes},{"candidates_visited",candidates},{"witness",witness},{"gap_mm",witness.is_null()?Json(nullptr):witness.at("gap_lower_bound_mm")}};
}
}
Json auditConvexSeparation(const std::vector<TriangleMeshReview>& parts,const ConvexSeparationBudget& budget){
    FloatingScope floatingScope;
    if(parts.empty()||parts.size()>32)throw std::runtime_error("Convex separation: part budget");
    std::vector<Part> prepared;for(const auto& part:parts)prepared.push_back(prepare(part));Json pairs=Json::array();size_t work=0;bool complete=true,passed=true;
    for(size_t i=0;i<prepared.size();++i)for(size_t k=0;k<i;++k){auto row=pair(prepared[k],prepared[i],budget,work);complete&=row.at("complete").get<bool>();passed&=row.at("status")=="separated";pairs.push_back(std::move(row));}
    return {{"schema","scdat.convex-separation/1"},{"method","bounded world/face/edge-cross axis search; outward interval projection of original affine vertices; gap / upper L1 axis norm"},{"minimum_gap_mm",1e-4},{"pair_count",pairs.size()},{"complete",complete},{"separation_gate_passed",complete&&passed},{"transport_allowed",false},{"overlap_classification_performed",false},{"vertex_projections",work},{"budget",{{"axes_per_pair",budget.axesPerPair},{"candidates_per_pair",budget.candidatesPerPair},{"vertex_projections",budget.vertexProjections}}},{"pairs",pairs}};
}
Json auditConvexSourceEnclosure(const std::vector<TriangleMeshReview>& parts,double radius){
    FloatingScope floatingScope;
    if(!std::isfinite(radius)||radius<=0)throw std::runtime_error("Convex source: invalid sphere radius");
    auto limit=multiply(exact(radius),exact(.99));double limitSquared=lower(limit.lo*limit.lo);bool passed=true;Json rows=Json::array();
    for(const auto& p:parts){double maximum=0;for(auto local:p.mesh.points){auto point=world(p,local);auto sum=exact(0);for(auto component:point){double bound=std::max(std::abs(component.lo),std::abs(component.hi));sum=add(sum,multiply(exact(bound),exact(bound)));}maximum=std::max(maximum,sum.hi);}bool enclosed=maximum<limitSquared;passed&=enclosed;rows.push_back({{"id",p.report.at("id")},{"maximum_vertex_radius_squared_upper_mm2",maximum},{"enclosed",enclosed}});}
    return {{"schema","scdat.convex-source-enclosure/1"},{"passed",passed},{"source_radius_mm",radius},{"enclosure_radius_lower_mm",limit.lo},{"enclosure_radius_squared_lower_mm2",limitSquared},{"method","outward interval affine vertices; squared radius upper bounds; convex-body enclosure by its vertices"},{"parts",rows}};
}
}
