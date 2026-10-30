#include "MeshMaterialShells.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Material shells: ")+message);}
TriangleMesh subset(const TriangleMesh& input,const Json& ids){
    TriangleMesh out;std::map<std::size_t,std::size_t> remap;
    for(const auto& item:ids){auto face=input.triangles.at(item.get<std::size_t>());for(auto& i:face){auto inserted=remap.emplace(i,out.points.size());if(inserted.second)out.points.push_back(input.points.at(i));i=inserted.first->second;}out.triangles.push_back(face);}return out;
}
}
MeshMaterialReview auditMeshMaterialShells(const TriangleMesh& mesh,const Json& declarations){
    require(declarations.is_array()&&!declarations.empty()&&declarations.size()<=16,"shell count must be 1..16");
    std::size_t references=0;for(const auto& d:declarations){require(d.is_object()&&d.size()==4&&d.contains("id")&&d.contains("role")&&d.contains("parent")&&d.contains("triangle_ids"),"exact shell fields required");require(d.at("id").is_string()&&std::regex_match(d.at("id").get<std::string>(),std::regex("[A-Za-z0-9_][A-Za-z0-9_.:/-]{0,255}")),"invalid shell ID");require(d.at("role")=="solid"||d.at("role")=="cavity","role must be solid or cavity");require(d.at("parent").is_null()||d.at("parent").is_string(),"parent must be ID or null");const auto& ids=d.at("triangle_ids");require(ids.is_array()&&!ids.empty()&&ids.size()<=mesh.triangles.size(),"nonempty shell face array required");references+=ids.size();require(references<=200000,"total shell reference budget");for(const auto& n:ids){require(n.is_number_integer()&&(n.is_number_unsigned()||n.get<std::int64_t>()>=0),"face IDs must be nonnegative integers");require(n.get<std::uint64_t>()<mesh.triangles.size(),"shell face ID outside mesh");}}
    MeshMaterialReview out;out.quality=auditTriangleMesh(mesh);out.spatial=auditMeshSpatial(mesh);
    Json blockers=Json::array(),issues=Json::array(),rows=Json::array();
    auto block=[&](const char* code,Json ids=Json::array(),Json components=Json::array()){if(std::find(blockers.begin(),blockers.end(),code)==blockers.end())blockers.push_back(code);if(issues.size()<128)issues.push_back({{"code",code},{"triangle_ids",ids},{"component_ids",components}});};
    std::vector<std::size_t> uses(mesh.triangles.size());std::set<std::string> names;std::size_t roots=0,root=0;
    for(std::size_t i=0;i<declarations.size();++i){const auto& d=declarations[i];auto id=d.at("id").get<std::string>();if(!names.insert(id).second)block("duplicate_shell_id",Json::array({d.at("triangle_ids")[0].get<std::size_t>()}));if(d.at("role")=="solid"){++roots;root=i;}for(const auto& n:d.at("triangle_ids"))++uses[n.get<std::size_t>()];rows.push_back({{"id",id},{"role",d.at("role")},{"parent",d.at("parent")},{"triangle_ids",d.at("triangle_ids")},{"component_id",nullptr},{"signed_boundary_volume_mm3",nullptr},{"enclosed_volume_mm3",nullptr},{"status","not_evaluated"}});}
    Json bad=Json::array();for(std::size_t f=0;f<uses.size();++f)if(uses[f]!=1&&bad.size()<16)bad.push_back(f);bool partition=bad.empty();if(!partition)block("shell_partition_not_exact",bad);
    if(roots!=1)block("exactly_one_solid_root_required");
    bool hierarchy=roots==1;if(hierarchy){auto rootID=declarations[root].at("id");for(std::size_t i=0;i<declarations.size();++i){const auto& d=declarations[i];bool ok=i==root?d.at("parent").is_null():(d.at("role")=="cavity"&&d.at("parent")==rootID);if(!ok){hierarchy=false;block("direct_root_cavity_hierarchy_required",Json::array({d.at("triangle_ids")[0].get<std::size_t>()}));}}}
    if(out.quality.at("closed_oriented_surface")!=true)block("surface_not_closed_or_consistently_oriented");if(out.quality.at("unused_welded_point_count")!=0)block("unused_vertices");
    if(out.spatial.at("complete")!=true)block("spatial_audit_incomplete");
    if(out.spatial.at("intersection_pair_count").get<std::size_t>()>0)block("triangle_intersections");
    std::vector<std::size_t> components(declarations.size());bool bound=partition&&!out.spatial.at("triangle_component_ids").is_null();
    if(bound){const auto& mapping=out.spatial.at("triangle_component_ids");std::set<std::size_t> claimed;for(std::size_t i=0;i<declarations.size();++i){const auto& ids=declarations[i].at("triangle_ids");auto component=mapping.at(ids[0].get<std::size_t>()).get<std::size_t>();bool exact=ids.size()==out.spatial.at("components").at(component).at("triangle_count").get<std::size_t>();for(const auto& id:ids)exact=exact&&mapping.at(id.get<std::size_t>())==component;if(!claimed.insert(component).second)exact=false;if(!exact){bound=false;block("shell_must_equal_one_component",Json::array({ids[0].get<std::size_t>()}),{component});}else{components[i]=component;rows[i]["component_id"]=component;}}if(claimed.size()!=out.spatial.at("components").size()){bound=false;block("undeclared_mesh_component");}}
    else block("shell_component_binding_unavailable");
    std::set<std::pair<std::size_t,std::size_t>> allowed,actual;for(const auto& relation:out.spatial.at("nesting_relations"))actual.insert({relation.at("inside_component").get<std::size_t>(),relation.at("outside_component").get<std::size_t>()});
    if(bound&&hierarchy){for(std::size_t i=0;i<declarations.size();++i)if(i!=root){auto relation=std::make_pair(components[i],components[root]);if(!actual.count(relation))block("cavity_not_strictly_inside_root",Json::array({declarations[i].at("triangle_ids")[0].get<std::size_t>()}),{components[i],components[root]});else allowed.insert(relation);}}
    for(const auto& issue:out.spatial.at("issues")){bool accepted=false;if(issue.at("code")=="nested_components"&&bound&&hierarchy)accepted=allowed.count({issue.at("component_ids")[0].get<std::size_t>(),issue.at("component_ids")[1].get<std::size_t>()})!=0;if(!accepted)block(issue.at("code")=="nested_components"?"unsupported_shell_nesting":"triangle_intersection_beyond_shared_simplex",issue.at("triangle_ids"),issue.at("component_ids"));}
    bool numeric=bound&&out.spatial.at("complete")==true&&out.quality.at("closed_oriented_surface")==true;
    if(numeric){for(std::size_t i=0;i<declarations.size();++i){auto volume=signedMeshBoundaryVolume(subset(mesh,declarations[i].at("triangle_ids")));rows[i]["signed_boundary_volume_mm3"]=volume;rows[i]["enclosed_volume_mm3"]=std::abs(volume);bool oriented=declarations[i].at("role")=="solid"?volume>0:volume<0;rows[i]["status"]=oriented?"orientation_verified":"wrong_orientation";if(!oriented)block("shell_orientation_wrong",Json::array({declarations[i].at("triangle_ids")[0].get<std::size_t>()}),{components[i]});}}
    Json net=nullptr;if(blockers.empty()){double value=signedMeshBoundaryVolume(mesh);if(!std::isfinite(value)||value<=0)block("positive_material_volume_required");else net=value;}
    bool passed=blockers.empty();for(auto& row:rows)row["status"]=passed?"verified":row.at("status")=="wrong_orientation"?"wrong_orientation":"rejected";
    out.semantics={{"schema","scdat.radiation-material-shell-review/1"},{"profile","one_solid_direct_cavities"},{"passed",passed},{"partition_exact",partition},{"component_binding_verified",bound},{"declared_hierarchy_valid",hierarchy},{"shells",rows},{"blockers",blockers},{"issues",issues},{"mesh_material_volume_mm3",net},{"volume_method","sum binary64 determinant expansions over oriented boundary; round once before divide by 6"},{"volume_is_transport_geometry",false}};return out;
}
}
