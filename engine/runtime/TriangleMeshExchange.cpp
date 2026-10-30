#include "TriangleMeshExchange.h"
#include "ProjectFile.h"
#include "MeshSpatialAudit.h"
#include "MeshMaterialShells.h"
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
constexpr qint64 maximumBytes=16*1024*1024;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error("Triangle mesh exchange: "+why);}
void fields(const Json& object,const std::set<std::string>& expected){require(object.is_object()&&object.size()==expected.size(),"missing/extra object fields");for(auto it=object.begin();it!=object.end();++it)require(expected.count(it.key())!=0,"unsupported field: "+it.key());}
std::string text(const Json& j,const char* key,std::size_t limit=256){require(j.at(key).is_string(),std::string("text required: ")+key);auto s=j.at(key).get<std::string>();require(!s.empty()&&s.size()<=limit&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;}),std::string("invalid text: ")+key);return s;}
Vector3 vector(const Json& j,double scale){require(j.is_array()&&j.size()==3,"three numeric coordinates required");Vector3 out{};for(int k=0;k<3;++k){require(j[k].is_number(),"numeric coordinate required");out[k]=j[k].get<double>()*scale;require(std::isfinite(out[k])&&std::abs(out[k])<=1e9,"coordinate/angle outside finite +/-1e9 budget");}return out;}
Json parse(const QByteArray& bytes){require(!bytes.isEmpty()&&bytes.size()<=maximumBytes,"JSON must be 1..16 MiB");std::vector<std::set<std::string>> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32,"JSON nesting exceeds 32");if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::key){require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"duplicate JSON key");}else if(event==Json::parse_event_t::object_end)keys.pop_back();return true;};return Json::parse(bytes.toStdString(),callback);
}
}
TriangleMeshReview inspectTriangleMeshBytes(const QByteArray& bytes,const QString& name,bool leafBvhRecheck){
    TriangleMeshReview out;auto j=parse(bytes);bool materialShells=j.value("schema",std::string{})=="scdat.radiation-triangle-mesh/2";if(materialShells){fields(j,{"schema","units","part","declared_source","shell_contract"});require(j.at("shell_contract")=="scdat.radiation-material-shells/1","unsupported shell contract");}else{fields(j,{"schema","units","part","declared_source"});require(j.at("schema")=="scdat.radiation-triangle-mesh/1","unsupported schema");}
    require(!leafBvhRecheck||!materialShells,"leaf-BVH recheck currently requires single-surface mesh/1; shell semantics remain on legacy profile");
    fields(j.at("units"),{"length","angle"});auto unit=text(j.at("units"),"length");require(unit=="mm"||unit=="cm"||unit=="m","length unit must be mm, cm or m");require(j.at("units").at("angle")=="deg","angle unit must be deg");double scale=unit=="mm"?1:unit=="cm"?10:1000;
    const auto& source=j.at("declared_source");fields(source,{"kind","description"});auto kind=text(source,"kind");require(kind=="external_mesh"||kind=="analytic_preview","unsupported source kind");text(source,"description",4096);
    const auto& part=j.at("part");if(materialShells)fields(part,{"id","material","sensitive","center","rotation_deg","points","triangles","shells"});else fields(part,{"id","material","sensitive","center","rotation_deg","points","triangles"});auto id=text(part,"id");require(std::regex_match(id,std::regex("[A-Za-z0-9_][A-Za-z0-9_.:/-]{0,255}")),"unsupported stable ID");auto material=text(part,"material");require(std::regex_match(material,std::regex("G4_[A-Za-z0-9_]+")),"NIST material identifier required");require(part.at("sensitive").is_boolean(),"sensitive must be Boolean");
    auto center=vector(part.at("center"),scale),angles=vector(part.at("rotation_deg"),1);const auto& points=part.at("points");const auto& triangles=part.at("triangles");require(points.is_array()&&points.size()>=3&&points.size()<=100000,"point budget 3..100000");require(triangles.is_array()&&!triangles.empty()&&triangles.size()<=200000,"triangle budget 1..200000");
    out.mesh.points.reserve(points.size());for(const auto& p:points)out.mesh.points.push_back(vector(p,scale));out.mesh.triangles.reserve(triangles.size());
    for(const auto& face:triangles){require(face.is_array()&&face.size()==3,"exactly three indices required");std::array<std::size_t,3> t{};for(int k=0;k<3;++k){const auto& n=face[k];require(n.is_number_integer(),"indices must be integers, not floats or strings");std::uint64_t i=0;if(n.is_number_unsigned())i=n.get<std::uint64_t>();else{auto signedIndex=n.get<std::int64_t>();require(signedIndex>=0,"negative vertex index");i=std::uint64_t(signedIndex);}require(i<points.size(),"vertex index outside point array");t[k]=std::size_t(i);}out.mesh.triangles.push_back(t);}
    // Reuse the established active Rz*Ry*Rx placement without whole-project
    // validation: a review mesh must not accidentally become transport input.
    Project probe;Volume v;v.id=id;v.center=center;v.rotation=angles;probe.volumes={v};out.placement=resolveGeometry(probe).at(0).transform;
    std::array<double,6> bounds{INFINITY,-INFINITY,INFINITY,-INFINITY,INFINITY,-INFINITY};for(const auto& p:out.mesh.points){auto world=out.placement.apply(p);for(int k=0;k<3;++k){require(std::isfinite(world[k])&&std::abs(world[k])<=1e9,"placed mesh exceeds finite +/-1e9 mm budget");bounds[2*k]=std::min(bounds[2*k],world[k]);bounds[2*k+1]=std::max(bounds[2*k+1],world[k]);}}
    Json quality,spatial,semantics=nullptr,blockers=Json::array();
    if(materialShells){auto review=auditMeshMaterialShells(out.mesh,part.at("shells"));quality=std::move(review.quality);spatial=std::move(review.spatial);semantics=std::move(review.semantics);blockers=semantics.at("blockers");}
    else{
    quality=auditTriangleMesh(out.mesh);if(quality.at("closed_oriented_surface")!=true)blockers.push_back("surface_not_closed_or_consistently_oriented");if(quality.at("edge_connected_component_count")!=1)blockers.push_back("single_component_required");if(quality.at("unused_welded_point_count")!=0)blockers.push_back("unused_vertices");if(quality.at("signed_volume_mm3").is_null()||quality.at("signed_volume_mm3").get<double>()<=0)blockers.push_back("positive_signed_volume_required");
    spatial=leafBvhRecheck?auditMeshSpatialLeafBvh(out.mesh):auditMeshSpatial(out.mesh);if(spatial.at("complete")!=true)blockers.push_back("spatial_audit_incomplete");if(spatial.at("intersection_pair_count").get<std::size_t>()>0)blockers.push_back("triangle_intersections");if(spatial.at("nested_component_pair_count").get<std::size_t>()>0)blockers.push_back("nested_components");
    }
    bool passes=blockers.empty();out.bundle=j;out.report={{"schema",leafBvhRecheck?"scdat.radiation-triangle-mesh-review/4":materialShells?"scdat.radiation-triangle-mesh-review/3":"scdat.radiation-triangle-mesh-review/2"},{"gate_version",leafBvhRecheck?"topology_and_single_triangle_leaf_bvh/1":materialShells?"topology_spatial_and_shell_semantics/1":"topology_and_spatial/1"},{"status",passes?"review_only":"rejected"},{"input",sourceFileIdentity(name,bytes)},
        {"declared_source",source},{"declared_source_verified",false},{"declared_length_unit",unit},{"length_to_mm",scale},{"normalized_length_unit","mm"},
        {"id",id},{"material",material},{"material_status","identifier_only_unresolved"},{"sensitive",part.at("sensitive")},{"center_mm",center},{"rotation_deg",angles},{"world_rotation",out.placement.rotation},{"world_bounds_mm",bounds},
        {"quality",quality},{"spatial",spatial},{"shell_semantics",semantics},{"review_issues",materialShells?semantics.at("issues"):spatial.at("issues")},{"geometry_gate_passed",passes},{"geometry_blockers",blockers},{"display_preview_allowed",true},{"transport_allowed",false},{"transport_mass_kg",nullptr},{"density_kg_m3",nullptr},
        {"transport_blockers",{"spatial_review_not_transport_certification","source_units_and_cad_fidelity_unverified","nist_material_unresolved","tessellated_transport_not_integrated"}},
        {"scope",materialShells?"explicit one-material outer boundary and direct cavities; review only, no repair or project import":"single indexed triangle surface; bounded topology/spatial review, no repair, no project import"}};return out;
}
TriangleMeshReview inspectTriangleMeshFile(const QString& path,bool leafBvhRecheck){QFile file(path);require(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<=maximumBytes,"cannot read mesh JSON <=16 MiB");auto bytes=file.read(maximumBytes+1);require(file.error()==QFileDevice::NoError,"mesh read error");return inspectTriangleMeshBytes(bytes,path,leafBvhRecheck);}
void writeTriangleMeshBundle(const QString& path,const Json& bundle){require(!QFile::exists(path),"refusing existing mesh output");auto bytes=QByteArray::fromStdString(bundle.dump(2));inspectTriangleMeshBytes(bytes,path);QSaveFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit(),"cannot atomically write mesh bundle");}
Json makeTriangleMeshBundle(const TriangleMesh& mesh,const std::string& id,const std::string& material,bool sensitive,const Vector3& center,const Vector3& angles,const Json& source){return {{"schema","scdat.radiation-triangle-mesh/1"},{"units",{{"length","mm"},{"angle","deg"}}},{"declared_source",source},{"part",{{"id",id},{"material",material},{"sensitive",sensitive},{"center",center},{"rotation_deg",angles},{"points",mesh.points},{"triangles",mesh.triangles}}}};}
}
