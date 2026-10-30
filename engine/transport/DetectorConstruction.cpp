#include "TransportState.h"
#include "Geometry.h"
#include "Materials.h"
#include "Voxels.h"
#include <G4PVParameterised.hh>
#include <G4VPVParameterisation.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4Box.hh>
#include <G4Tubs.hh>
#include <G4Orb.hh>
#include <G4SubtractionSolid.hh>
#include <G4NistManager.hh>
#include <G4LogicalVolume.hh>
#include <G4PVPlacement.hh>
#include <G4SystemOfUnits.hh>
#include <stdexcept>
#include <cmath>
namespace spacerad::engine::transport {
namespace {
// Generic Boolean volume estimation samples random points. For this restricted,
// concentric box difference the exact volume is known from the actual solids;
// use it for mass auditing without disturbing the transport random stream.
class BoxShell final:public G4SubtractionSolid {
public:
    BoxShell(const G4String& name,G4Box* a,G4Box* b):G4SubtractionSolid(name,a,b){
        // G4BooleanSolid exposes this protected cache; the final subtraction
        // getter returns it. Both constituent boxes are concentric by contract.
        fCubicVolume=a->GetCubicVolume()-b->GetCubicVolume();
    }
};
class VoxelCopies final:public G4VPVParameterisation {
    std::array<int,3> counts;Vector3 spacing,origin;
public:
    explicit VoxelCopies(const Json& layout):counts(layout.at("counts").get<std::array<int,3>>()),spacing(layout.at("spacing_mm").get<Vector3>()),origin(layout.at("origin_local_mm").get<Vector3>()){}
    void ComputeTransformation(G4int copy,G4VPhysicalVolume* pv)const override{
        std::array<int,3> ijk{copy%counts[0],(copy/counts[0])%counts[1],copy/(counts[0]*counts[1])};
        pv->SetTranslation({(origin[0]+(ijk[0]+.5)*spacing[0])*mm,(origin[1]+(ijk[1]+.5)*spacing[1])*mm,(origin[2]+(ijk[2]+.5)*spacing[2])*mm});pv->SetRotation(nullptr);
    }
    void ComputeDimensions(G4Box& box,G4int,const G4VPhysicalVolume*)const override{box.SetXHalfLength(spacing[0]/2*mm);box.SetYHalfLength(spacing[1]/2*mm);box.SetZHalfLength(spacing[2]/2*mm);}
};
}
class Geometry final:public G4VUserDetectorConstruction {
    State& state;
public:
    explicit Geometry(State& s):state(s) {}
    G4VPhysicalVolume* Construct() override {
        auto* nist=G4NistManager::Instance();
        auto r=state.project.radius*1.25*mm;
        auto* world=new G4LogicalVolume(new G4Box("World",r,r,r),nist->FindOrBuildMaterial("G4_Galactic"),"World");
        auto* pv=new G4PVPlacement(nullptr,{},world,"World",nullptr,false,0);
        state.geometryAudit=geometrySnapshot(state.project);
        auto geometry=compileGeometry(state.project);
        for(const auto& resolved:geometry.placements) {
            const auto& v=geometry.volumes[resolved.index];
            G4VSolid* solid;
            if(v.shape=="cylinder")solid=new G4Tubs(v.id,0,v.size[0]/2*mm,v.size[2]/2*mm,0,2*pi);
            else if(v.shape=="sphere")solid=new G4Orb(v.id,v.size[0]/2*mm);
            else solid=new G4Box(v.id,v.size[0]/2*mm,v.size[1]/2*mm,v.size[2]/2*mm);
            if(v.wall>0) {
                auto* cavity=new G4Box(v.id+"_cavity",(v.size[0]/2-v.wall)*mm,
                    (v.size[1]/2-v.wall)*mm,(v.size[2]/2-v.wall)*mm);
                solid=new BoxShell(v.id+"_shell",static_cast<G4Box*>(solid),cavity);
            }
            auto* mat=nist->FindOrBuildMaterial(v.material,false);
            if(!mat) throw std::runtime_error("Unknown NIST material for volume "+v.id+": "+v.material);
            auto* lv=new G4LogicalVolume(solid,mat,v.id);
            const auto& t=resolved.transform;const auto& m=t.rotation;
            G4PVPlacement* placement;
            if(m==Matrix3{{{1,0,0},{0,1,0},{0,0,1}}}){
                // Preserve the legacy unrotated placement and random-number sequence.
                placement=new G4PVPlacement(nullptr,{t.translation[0]*mm,t.translation[1]*mm,t.translation[2]*mm},lv,v.id,world,false,0);
            }else{
                G4RotationMatrix rotation({m[0][0],m[1][0],m[2][0]},
                    {m[0][1],m[1][1],m[2][1]},{m[0][2],m[1][2],m[2][2]});
                placement=new G4PVPlacement(G4Transform3D(rotation,{t.translation[0]*mm,t.translation[1]*mm,t.translation[2]*mm}),lv,v.id,world,false,0);
            }
            if(placement->CheckOverlaps(3000,0,true,1)) throw std::runtime_error("Geant4 overlap check failed: "+v.id);
            const auto actualRotation=placement->GetObjectRotationValue();const auto actualCenter=placement->GetObjectTranslation();
            Matrix3 actualMatrix{};Vector3 center{};double error=0;
            for(int i=0;i<3;++i){center[i]=actualCenter[i]/mm;error=std::max(error,std::abs(center[i]-t.translation[i]));
                for(int j=0;j<3;++j){actualMatrix[i][j]=actualRotation(i,j);error=std::max(error,std::abs(actualMatrix[i][j]-m[i][j]));}}
            if(error>1e-9)throw std::runtime_error("Geant4 placement differs from resolved geometry: "+v.id);
            double mass=lv->GetMass()/kg,volume=solid->GetCubicVolume()/(mm*mm*mm);
            auto& audit=state.geometryAudit["volumes"][resolved.index];
            if(!std::isfinite(mass)||mass<=0||std::abs(volume-audit.at("material_volume_mm3").get<double>())>1e-8*volume)
                throw std::runtime_error("Geant4 material volume/mass audit failed: "+v.id);
            audit["geant4_world_center_mm"]=center;audit["geant4_world_rotation"]=actualMatrix;
            audit["geant4_material_volume_mm3"]=volume;audit["mass_kg"]=mass;
            audit["volume_method"]=v.shape=="cylinder"?"G4Tubs analytic volume":v.shape=="sphere"?"G4Orb analytic volume":v.wall>0?"exact concentric constituent box difference cached in G4BooleanSolid":"G4Box analytic volume";
            if(hasAnalyticGeometry(state.project)){G4ThreeVector low,high;solid->BoundingLimits(low,high);
                audit["geant4_local_bounds_mm"]={low.x()/mm,high.x()/mm,low.y()/mm,high.y()/mm,low.z()/mm,high.z()/mm};}
            audit["density_kg_m3"]=mat->GetDensity()/(kg/m3);
            if(v.sensitive) state.scores[v.id].mass=mass;
            if(!state.project.voxelScoring.is_null()&&voxelSpec(state.project.voxelScoring).volumeId==v.id){
                auto layout=voxelLayout(state.project);auto spec=voxelSpec(state.project.voxelScoring);auto d=layout.at("spacing_mm").get<Vector3>();
                auto* cellSolid=new G4Box(v.id+"__voxel_solid",d[0]/2*mm,d[1]/2*mm,d[2]/2*mm);
                auto* cell=new G4LogicalVolume(cellSolid,mat,v.id+"__voxel_logical");
                state.voxelPhysical=new G4PVParameterised(v.id+"__voxel",cell,lv,kUndefined,spec.size(),new VoxelCopies(layout),false);
                double cellMass=cell->GetMass()/kg;
                if(std::abs(cellMass*spec.size()-mass)>1e-10*mass)throw std::runtime_error("Voxel subdivision changes target material mass");
                state.voxelAudit=layout;state.voxelAudit["cell_mass_kg"]=cellMass;state.voxelAudit["target_mass_kg"]=mass;
                state.voxelAudit["density_kg_m3"]=mat->GetDensity()/(kg/m3);
                state.voxelAudit["construction"]="G4PVParameterised homogeneous daughter boxes; exact full-volume tiling; no sampled mixed-material mass";
                state.voxelAudit["overlap_check"]="Analytic equal-cell tiling and actual cell/target mass conservation; no stochastic all-pairs daughter overlap check";
                state.voxelMoments.resize(spec.size());
            }
        }
        state.materialAudit=auditMaterialSnapshot(state.project);
        return pv;
    }
};

G4VUserDetectorConstruction* makeGeometry(State& s) { return new Geometry(s); }
}
