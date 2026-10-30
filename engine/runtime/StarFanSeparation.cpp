#include "StarFanSeparation.h"
#include "NonconvexAssembly.h"
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <cfenv>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif
namespace spacerad::engine {
namespace {
void need(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Star fan: ")+m);}
struct FloatingScope {
#if defined(_M_X64) || defined(__SSE2__)
    unsigned saved=_mm_getcsr();
    FloatingScope(){_mm_setcsr(saved&~0xe040u);}
    ~FloatingScope(){_mm_setcsr(saved);}
#else
    FloatingScope(){need(std::fegetround()==FE_TONEAREST,"nearest rounding required");}
#endif
};
static_assert(std::numeric_limits<double>::is_iec559&&sizeof(double)==8,"IEEE binary64 required");
struct I {double lo,hi;};
double down(double x){need(std::isfinite(x),"nonfinite interval operation");return std::abs(x)<std::numeric_limits<double>::min()?-std::numeric_limits<double>::min():std::nextafter(x,-std::numeric_limits<double>::infinity());}
double up(double x){need(std::isfinite(x),"nonfinite interval operation");return std::abs(x)<std::numeric_limits<double>::min()?std::numeric_limits<double>::min():std::nextafter(x,std::numeric_limits<double>::infinity());}
I sub(I a,I b){return {down(a.lo-b.hi),up(a.hi-b.lo)};}
I add(I a,I b){return {down(a.lo+b.lo),up(a.hi+b.hi)};}
I mul(I a,I b){double v[]={a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};return {down(*std::min_element(v,v+4)),up(*std::max_element(v,v+4))};}
I determinant(Vector3 a,Vector3 b,Vector3 c,Vector3 k){
    std::array<I,3> x,y,z;for(int d=0;d<3;++d){x[d]=sub({a[d],a[d]},{k[d],k[d]});y[d]=sub({b[d],b[d]},{k[d],k[d]});z[d]=sub({c[d],c[d]},{k[d],k[d]});}
    return add(sub(mul(x[0],sub(mul(y[1],z[2]),mul(y[2],z[1]))),mul(x[1],sub(mul(y[0],z[2]),mul(y[2],z[0])))),mul(x[2],sub(mul(y[0],z[1]),mul(y[1],z[0]))));
}
void fields(const Json& j,std::set<std::string> names){need(j.is_object()&&j.size()==names.size(),"missing/extra cover fields");for(auto i=j.begin();i!=j.end();++i)need(names.count(i.key())!=0,"unknown cover field");}
struct Cover {std::vector<TriangleMeshReview> cells;Json audit;bool qualified=false;};
Cover prepare(const TriangleMeshReview& p,const Json& spec,size_t& work,const StarFanBudget& budget){
    need(spec.at("part_id")==p.report.at("id"),"cover ID/order mismatch");Cover out;auto kind=spec.at("kind");
    if(kind=="vertex_hull"){fields(spec,{"part_id","kind"});out.cells={p};out.qualified=true;out.audit={{"part_id",p.report.at("id")},{"kind",kind},{"qualified",true},{"complete",true},{"cell_count",1},{"boundary_chain_verified",nullptr},{"kernel_local_mm",nullptr},{"face_determinants",Json::array()}};return out;}
    fields(spec,{"part_id","kind","kernel_local_mm"});need(kind=="star_fan","unsupported cover kind");const auto& kj=spec.at("kernel_local_mm");need(kj.is_array()&&kj.size()==3,"kernel requires three local mm coordinates");Vector3 kernel;for(int d=0;d<3;++d){need(kj[d].is_number(),"numeric kernel required");kernel[d]=kj[d].get<double>();need(std::isfinite(kernel[d])&&std::abs(kernel[d])<=10000,"kernel coordinate budget");}
    std::map<std::array<size_t,2>,std::pair<int,int>> edges;
    for(auto t:p.mesh.triangles)for(int d=0;d<3;++d){size_t a=t[d],b=t[(d+1)%3];auto& e=edges[{std::min(a,b),std::max(a,b)}];++e.first;e.second+=a<b?1:-1;}
    bool chain=true;for(auto e:edges)chain&=e.second.first==2&&e.second.second==0;
    bool positive=true,complete=true;Json faces=Json::array();
    for(size_t i=0;i<p.mesh.triangles.size();++i){if(work>=budget.kernelFaces){complete=false;break;}++work;auto t=p.mesh.triangles[i];auto det=determinant(p.mesh.points[t[0]],p.mesh.points[t[1]],p.mesh.points[t[2]],kernel);positive&=det.lo>0;
        faces.push_back({{"triangle",i},{"determinant_interval_mm3",{det.lo,det.hi}},{"strictly_positive",det.lo>0}});
        TriangleMeshReview cell;cell.report={{"id",p.report.at("id")}};cell.placement=p.placement;cell.mesh.points={kernel,p.mesh.points[t[0]],p.mesh.points[t[1]],p.mesh.points[t[2]]};cell.mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};out.cells.push_back(std::move(cell));
    }
    out.qualified=complete&&chain&&positive;out.audit={{"part_id",p.report.at("id")},{"kind",kind},{"qualified",out.qualified},{"complete",complete},{"cell_count",out.cells.size()},{"boundary_chain_verified",chain},{"kernel_local_mm",kernel},{"face_determinants",faces},{"method","Strict outward interval det(a-k,b-k,c-k)>0 for every face; each original edge occurs twice with opposite orientation, cancelling all internal fan faces. Positive tetra chains have the original outward surface as boundary, hence cover its bounded interior by winding number."}};return out;
}
}
Json auditStarFanSeparation(const std::vector<TriangleMeshReview>& parts,const Json& specs,const StarFanBudget& budget){
    FloatingScope fp;need(parts.size()>=2&&parts.size()<=16&&specs.is_array()&&specs.size()==parts.size(),"one ordered cover per assembly part required");
    std::vector<Cover> covers;Json ca=Json::array(),pairs=Json::array();size_t faces=0,work=0,cells=0;bool qualified=true,complete=true,passed=true;
    for(size_t i=0;i<parts.size();++i){auto c=prepare(parts[i],specs[i],faces,budget);qualified&=c.qualified;complete&=c.audit.at("complete").get<bool>();ca.push_back(c.audit);covers.push_back(std::move(c));}
    for(size_t i=0;i<parts.size();++i)for(size_t k=0;k<i;++k){
        Json row={{"a",parts[k].report.at("id")},{"b",parts[i].report.at("id")},{"status","unqualified_cover"},{"complete",true},{"axes_evaluated",0},{"candidates_visited",0},{"witness",nullptr},{"gap_mm",nullptr},{"method","cell_pairs"},{"cell_witnesses",Json::array()},{"cell_pairs_total",covers[k].cells.size()*covers[i].cells.size()},{"cell_pairs_tested",0}};
        if(covers[k].qualified&&covers[i].qualified){
            auto test=[&](const TriangleMeshReview& a,const TriangleMeshReview& b,size_t axes,size_t candidates){ConvexSeparationBudget cb;cb.axesPerPair=axes;cb.candidatesPerPair=candidates;cb.vertexProjections=budget.vertexProjections-work;auto r=auditConvexSeparation({a,b},cb);work+=r.at("vertex_projections").get<size_t>();auto q=r.at("pairs")[0];row["axes_evaluated"]=row.at("axes_evaluated").get<size_t>()+q.at("axes_evaluated").get<size_t>();row["candidates_visited"]=row.at("candidates_visited").get<size_t>()+q.at("candidates_visited").get<size_t>();return q;};
            auto whole=test(parts[k],parts[i],3,3);
            if(whole.at("status")=="separated"){row["status"]="separated";row["method"]="whole_vertex_hull";row["witness"]=whole.at("witness");row["gap_mm"]=whole.at("gap_mm");}
            else {bool ok=true,done=true;double gap=std::numeric_limits<double>::infinity();std::string status="separated";
                for(size_t a=0;a<covers[k].cells.size()&&ok;++a)for(size_t b=0;b<covers[i].cells.size()&&ok;++b){
                    if(cells>=budget.cellPairs){ok=false;done=false;status="budget_exhausted";break;}++cells;auto q=test(covers[k].cells[a],covers[i].cells[b],4096,50000);q["a_cell"]=a;q["b_cell"]=b;row["cell_witnesses"].push_back(q);row["cell_pairs_tested"]=row.at("cell_pairs_tested").get<size_t>()+1;
                    if(q.at("status")!="separated"){ok=false;done=q.at("complete");status=q.at("status");}else gap=std::min(gap,q.at("gap_mm").get<double>());
                }
                row["status"]=status;row["complete"]=done;if(ok)row["gap_mm"]=gap;
            }
        }
        complete&=row.at("complete").get<bool>();passed&=row.at("status")=="separated";pairs.push_back(row);
    }
    return {{"schema","scdat.star-fan-separation/1"},{"method","Certified positive star-fan tetra covers or enclosing vertex hulls; every cross-body cell pair needs an outward interval strict gap. Audit cells never become transport geometry."},{"minimum_gap_mm",1e-4},{"cover_gate_passed",qualified},{"separation_gate_passed",qualified&&complete&&passed},{"complete",complete},{"covers",ca},{"pair_count",pairs.size()},{"pairs",pairs},{"kernel_faces_evaluated",faces},{"cell_pairs_evaluated",cells},{"vertex_projections",work},{"budget",{{"cell_pairs",budget.cellPairs},{"vertex_projections",budget.vertexProjections},{"kernel_faces",budget.kernelFaces}}},{"transport_allowed",false},{"overlap_classification_performed",false}};
}
Json starFanAssemblyDemo(const std::string& particle){
    auto j=nonconvexAssemblyDemo(particle);j["schema"]="scdat.mesh-experiment/6";j["separation_contract"]="scdat.star-fan-separation/1";j["covers"]=Json::array();
    for(size_t i=0;i<4;++i){auto& b=j["parts"][i];auto& p=b["part"];p["center"]=i==0?Vector3{0,0,0}:i==1?Vector3{35,0,0}:i==2?Vector3{5,5,0}:Vector3{30,-5,0};p["rotation_deg"]=Vector3{0,0,i==1?180.:0.};
        j["origins"][i]["mesh_sha256"]=QCryptographicHash::hash(QByteArray::fromStdString(b.dump()),QCryptographicHash::Sha256).toHex().toStdString();
        Json cover={{"part_id",p.at("id")},{"kind",i<2?"star_fan":"vertex_hull"}};if(i<2)cover["kernel_local_mm"]=Vector3{-5,-5,0};j["covers"].push_back(cover);
    }
    j["run"]["source_radius_mm"]=70;j["run"]["histories"]=80000;return j;
}
}
