#include "MeshExperimentTransport.h"
#include "TransportState.h"
#include "Materials.h"
#include "runtime/ConvexSeparation.h"
#include "runtime/NonconvexAssembly.h"
#include "runtime/StarFanSeparation.h"
#include <G4ExtrudedSolid.hh>
#include <G4VFacet.hh>
#include <G4TessellatedSolid.hh>
#include <G4TriangularFacet.hh>
#include <G4Box.hh>
#include <G4NistManager.hh>
#include <G4LogicalVolume.hh>
#include <G4PVPlacement.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4VModularPhysicsList.hh>
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <G4RunManager.hh>
#include <G4SystemOfUnits.hh>
#include <Randomize.hh>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace spacerad::engine {
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(std::string("Mesh transport: ")+why);}
G4ThreeVector point(const Vector3& p){return {p[0]*mm,p[1]*mm,p[2]*mm};}
class Geometry final:public G4VUserDetectorConstruction {
    transport::State& state;const MeshExperimentJob& job;Json nativeCovers;
public:
    Geometry(transport::State& s,const MeshExperimentJob& j):state(s),job(j),nativeCovers(j.starFanAssembly?j.document.at("covers"):Json::array()){}
    Json addPart(const TriangleMeshReview& part,size_t index,G4LogicalVolume* world,std::vector<G4PVPlacement*>& placements,std::vector<TriangleMeshReview>& nativeParts){
        auto* nist=G4NistManager::Instance();const auto& mesh=part.mesh;std::string id=part.report.at("id");Vector3 lo=mesh.points[0],hi=lo;
        for(auto p:mesh.points)for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
        G4VSolid* solid=nullptr;Vector3 offset{};bool lReference=job.nonconvexAssembly&&job.analyticReference&&job.referenceProfiles.at(index)=="orthogonal_l";
        if(lReference){std::vector<G4TwoVector> polygon{{-10*mm,-10*mm},{10*mm,-10*mm},{10*mm,0},{0,0},{0,10*mm},{-10*mm,10*mm}};solid=new G4ExtrudedSolid(id,polygon,2*mm);}
        else if(job.analyticReference){solid=new G4Box(id,(hi[0]-lo[0])/2*mm,(hi[1]-lo[1])/2*mm,(hi[2]-lo[2])/2*mm);for(int k=0;k<3;++k)offset[k]=(hi[k]+lo[k])/2;}
        else{auto* tess=new G4TessellatedSolid(id);for(auto t:mesh.triangles)need(tess->AddFacet(new G4TriangularFacet(point(mesh.points[t[0]]),point(mesh.points[t[1]]),point(mesh.points[t[2]]),ABSOLUTE)),"facet construction rejected");tess->SetSolidClosed(true);need(tess->GetSolidClosed()&&tess->CheckStructure()==0,"solid closure/structure failed");solid=tess;}
        // Face probes follow complete topology/self-intersection checks.
        // v1-v3 additionally require convexity; explicit single-surface v4 does not.
        int probes=0;const double epsilon=1e-5*mm;
        for(auto t:mesh.triangles){auto a=point(mesh.points[t[0]]),b=point(mesh.points[t[1]]),c=point(mesh.points[t[2]]);auto n=(b-a).cross(c-a).unit();auto center=(a+b+c)/3-point(offset);auto outside=center+epsilon*n,inside=center-epsilon*n;
            need(solid->Inside(outside)==kOutside&&solid->Inside(inside)==kInside,"facet inside/outside probe failed");
            double enter=solid->DistanceToIn(outside,-n),leave=solid->DistanceToOut(inside,n);
            need(std::isfinite(enter)&&std::isfinite(leave)&&std::abs(enter-epsilon)<epsilon*.01&&std::abs(leave-epsilon)<epsilon*.01,"facet navigation distance mismatch");++probes;}
        auto* material=nist->FindOrBuildMaterial(part.report.at("material").get<std::string>(),false);need(material!=nullptr,"unknown NIST material");auto* lv=new G4LogicalVolume(solid,material,id);
        const auto& transform=part.placement;const auto& m=transform.rotation;auto translation=transform.apply(offset);
        G4RotationMatrix rotation({m[0][0],m[1][0],m[2][0]},{m[0][1],m[1][1],m[2][1]},{m[0][2],m[1][2],m[2][2]});
        auto* placed=new G4PVPlacement(G4Transform3D(rotation,point(translation)),lv,id,world,false,0);
        auto actualRotation=placed->GetObjectRotationValue();auto actualTranslation=placed->GetObjectTranslation();double error=0;Matrix3 actualMatrix{};Vector3 actualCenter{};
        for(int i=0;i<3;++i){actualCenter[i]=actualTranslation[i]/mm;error=std::max(error,std::abs(actualCenter[i]-translation[i]));for(int j=0;j<3;++j){actualMatrix[i][j]=actualRotation(i,j);error=std::max(error,std::abs(actualMatrix[i][j]-m[i][j]));}}
        need(error<1e-9,"actual placement mismatch");placements.push_back(placed);
        double volume=solid->GetCubicVolume()/(mm*mm*mm),expected=part.report.at("quality").at("signed_volume_mm3"),mass=lv->GetMass()/kg,density=material->GetDensity()/(kg/m3);
        need(std::isfinite(volume)&&volume>0&&std::abs(volume-expected)<=1e-9*expected,"actual solid volume mismatch");need(std::isfinite(mass)&&mass>0&&std::abs(mass-volume*1e-9*density)<=1e-10*mass,"actual mass mismatch");if(part.report.at("sensitive")==true)state.scores[id].mass=mass;
        if(job.convexSeparation||job.singleSurface||job.nonconvexAssembly){
            TriangleMeshReview native;native.report={{"id",id}};native.placement.rotation=actualMatrix;native.placement.translation=actualCenter;
            if(job.analyticReference&&!lReference){auto* box=static_cast<G4Box*>(solid);double x=box->GetXHalfLength()/mm,y=box->GetYHalfLength()/mm,z=box->GetZHalfLength()/mm;native.mesh.points={{-x,-y,-z},{x,-y,-z},{x,y,-z},{-x,y,-z},{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}};native.mesh.triangles={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};}
            else{auto* tess=static_cast<G4TessellatedSolid*>(solid);std::map<Vector3,size_t> indices;for(int i=0;i<tess->GetNumberOfFacets();++i){auto* facet=tess->GetFacet(i);int count=facet->GetNumberOfVertices();need(count==3||(lReference&&count==4),"unsupported native facet");std::array<size_t,4> tri{};for(int k=0;k<count;++k){auto v=facet->GetVertex(k);Vector3 p{v.x()/mm,v.y()/mm,v.z()/mm};auto inserted=indices.emplace(p,native.mesh.points.size());if(inserted.second)native.mesh.points.push_back(p);tri[k]=inserted.first->second;}native.mesh.triangles.push_back({tri[0],tri[1],tri[2]});if(count==4)native.mesh.triangles.push_back({tri[0],tri[2],tri[3]});}}
            if(job.singleSurface||(job.nonconvexAssembly&&!job.analyticReference)){need(native.mesh.triangles.size()==mesh.triangles.size(),"native facet count differs");for(size_t i=0;i<mesh.triangles.size();++i)for(int k=0;k<3;++k)for(int d=0;d<3;++d)need(native.mesh.points[native.mesh.triangles[i][k]][d]==mesh.points[mesh.triangles[i][k]][d],"native oriented facet differs from original mesh");}
            if(job.starFanAssembly&&nativeCovers[index].at("kind")=="star_fan"){auto kernel=nativeCovers[index].at("kernel_local_mm").get<Vector3>();for(int d=0;d<3;++d)kernel[d]-=offset[d];nativeCovers[index]["kernel_local_mm"]=kernel;}
            nativeParts.push_back(std::move(native));
        }
        return {{"schema",(job.singleSurface||job.nonconvexAssembly)?"scdat.experimental-single-surface-geometry/1":"scdat.experimental-convex-geometry/1"},{"id",id},{"material",part.report.at("material")},{"sensitive",part.report.at("sensitive")},{"solid",lReference?"G4ExtrudedSolid L reference":job.analyticReference?"G4Box analytic reference":"G4TessellatedSolid"},{"convex_single_body",!job.singleSurface&&!job.nonconvexAssembly},{"facet_navigation_probes",probes},{"facet_probe_offset_mm",epsilon/mm},{"surface_overlap_samples",3000},{"overlap_detected",false},{"placement_max_error",error},{"geant4_world_center_mm",actualCenter},{"geant4_world_rotation",actualMatrix},{"mesh_local_signed_volume_mm3",expected},{"geant4_volume_mm3",volume},{"mass_kg",mass},{"density_kg_m3",density},{"triangle_count",mesh.triangles.size()},{"experimental_transport_allowed",true},{"general_cad_transport_allowed",false}};
    }
    G4VPhysicalVolume* Construct()override{
        auto* nist=G4NistManager::Instance();double r=state.project.radius*1.25*mm;
        auto* world=new G4LogicalVolume(new G4Box("World",r,r,r),nist->FindOrBuildMaterial("G4_Galactic"),"World");auto* worldPv=new G4PVPlacement(nullptr,{},world,"World",nullptr,false,0);
        Json volumes=Json::array();std::vector<G4PVPlacement*> placements;std::vector<TriangleMeshReview> nativeParts;
        for(size_t i=0;i<job.parts.size();++i)volumes.push_back(addPart(job.parts[i],i,world,placements,nativeParts));
        // Recheck only after all siblings exist, so early parts see later ones too.
        for(auto* placed:placements)need(!placed->CheckOverlaps(3000,0,true,1),"complete-scene Geant4 overlap check failed");
        if(job.assembly)state.geometryAudit={{"schema",job.starFanAssembly?"scdat.experimental-nonconvex-geometry/6":job.nonconvexAssembly?"scdat.experimental-nonconvex-geometry/5":job.convexSeparation?"scdat.experimental-convex-geometry/3":"scdat.experimental-convex-geometry/2"},{"volumes",volumes},{"part_count",volumes.size()},{"separation_audit",job.separationAudit},{"overlap_check_after_all_placements",true},{"experimental_transport_allowed",true},{"general_cad_transport_allowed",false}};
        else state.geometryAudit=volumes.at(0);
        if(job.nonconvexAssembly){state.geometryAudit["convexity_required"]=false;state.geometryAudit["single_connected_surface_per_part"]=true;state.geometryAudit["native_oriented_facets_equal_input"]=!job.analyticReference;state.geometryAudit["original_assembly_restored"]=false;}
        if(job.singleSurface){state.geometryAudit["convexity_required"]=false;state.geometryAudit["single_connected_surface"]=true;state.geometryAudit["native_oriented_facets_equal_input"]=true;state.geometryAudit["cad_provenance"]=job.document.at("cad_provenance");}
        if(job.convexSeparation||job.singleSurface||job.nonconvexAssembly){auto nativeSeparation=job.starFanAssembly?auditStarFanSeparation(nativeParts,nativeCovers):job.nonconvexAssembly?auditVertexHullSeparation(nativeParts):job.singleSurface?job.separationAudit:auditConvexSeparation(nativeParts);auto nativeEnclosure=auditConvexSourceEnclosure(nativeParts,state.project.radius);if(job.singleSurface||job.nonconvexAssembly)nativeEnclosure["method"]=job.sourceEnclosure.at("method");need(nativeSeparation.at("separation_gate_passed")==true&&nativeEnclosure.at("passed")==true,"actual native geometry separation/source enclosure failed");state.geometryAudit["native_separation_audit"]=nativeSeparation;state.geometryAudit["native_source_enclosure"]=nativeEnclosure;state.geometryAudit["native_parts"]=Json::array();for(const auto& part:nativeParts)state.geometryAudit["native_parts"].push_back({{"id",part.report.at("id")},{"local_points_mm",part.mesh.points},{"triangles",part.mesh.triangles},{"world_rotation",part.placement.rotation},{"world_center_mm",part.placement.translation}});state.geometryAudit["source_enclosure"]=job.sourceEnclosure;state.geometryAudit["geometry_authority"]="Original job.parts affine vertices plus Geant4-read-back native facets/box corners and placements; source Project proxies never construct geometry or mass";}
        if(job.starFanAssembly){state.geometryAudit["audit_cells_are_physical_volumes"]=false;state.geometryAudit["native_cover_specifications"]=nativeCovers;}
        state.materialAudit=transport::auditMaterialSnapshot(state.project);return worldPv;
    }
};
}
Json runMeshExperiment(const MeshExperimentJob& job,const Json& runtime){
    job.source.validate();if(job.convexSeparation||job.singleSurface||job.nonconvexAssembly)need(job.separationAudit.at("separation_gate_passed")==true&&job.sourceEnclosure.at("passed")==true,"blocked convex separation/source inspection cannot run");
    auto started=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();transport::State state(job.source);auto manager=std::make_unique<G4RunManager>();
    G4Random::setTheSeed(job.source.seed);manager->SetUserInitialization(new Geometry(state,job));manager->SetUserInitialization(transport::makePhysics(job.source));manager->SetUserAction(transport::makeSource(state));manager->SetUserAction(transport::makeEventAction(state));manager->SetUserAction(transport::makeStepAction(state));manager->Initialize();
    // Keep random geometry sampling out of the source/transport sequence for both comparator modes.
    G4Random::setTheSeed(job.source.seed);manager->BeamOn(job.source.events);need(state.sourceAudit.mu.count==job.source.events,"incomplete source histories");size_t sensitive=0;for(const auto& part:job.parts)if(part.report.at("sensitive")==true)++sensitive;need(state.scores.size()==sensitive,"sensitive scoring count mismatch");for(const auto& e:state.scores)need(e.second.ionizing.count==job.source.events,"incomplete scoring histories");
    auto result=transport::buildScores(state);QFile exe(QCoreApplication::applicationFilePath());need(exe.open(QIODevice::ReadOnly),"cannot hash Worker");result["schema"]=job.starFanAssembly?"scdat.mesh-experiment-result/6":job.nonconvexAssembly?"scdat.mesh-experiment-result/5":job.singleSurface?"scdat.mesh-experiment-result/4":job.convexSeparation?"scdat.mesh-experiment-result/3":job.assembly?"scdat.mesh-experiment-result/2":"scdat.mesh-experiment-result/1";result["job"]=job.document;result["job_sha256"]=QCryptographicHash::hash(job.bytes,QCryptographicHash::Sha256).toHex().toStdString();result["worker_sha256"]=QCryptographicHash::hash(exe.readAll(),QCryptographicHash::Sha256).toHex().toStdString();result["runtime_audit"]=runtime;result["engine_version"]=runtime.at("engine_version");if(job.assembly){result["mesh_reviews"]=Json::array();for(const auto& part:job.parts)result["mesh_reviews"].push_back(part.report);}else result["mesh_review"]=job.mesh.report;result["started_utc"]=started;result["finished_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();result["synthetic"]=true;result["mission_prediction"]=false;
    result["effective_configuration"]={{"physics_list",job.source.physics},{"production_cut_mm",job.source.cut},{"seed",job.source.seed},{"histories",job.source.events},{"threading","serial"},{"geometry_rng_reset_before_transport",true}};
    result["limitations"]={"v6 permits only certified strict-kernel positive tetra covers or vertex-hull covers with strict cross-cell gaps. Audit cells do not construct physical volumes; arbitrary non-star-shaped concavity/contact/cavities remain restricted. v5 permits separated vertex hulls of individually qualified nonconvex surfaces, not interlocking/contact/cavity assemblies; user-defined CAD instances do not restore original assembly. v4 permits only one connected closed surface with full self-intersection review; no multi-body contact or separate cavity shells. v1-v3 retain restricted separated convex-part experiment; v2 AABB gaps or v3 outward-interval axis witnesses; no contact, separate cavity shells or general CAD approval","Synthetic flat differential fluence; no orbit or AP8/AE8 execution","TID and Monte Carlo standard error only; no external physics validation, device DDD or SEE failure model","Fixed facet probes and sampled overlap checks are numerical regressions, not complete geometry certification"};return result;
}
}
