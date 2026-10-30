#include "TransportState.h"
#include "runtime/VoxelStatistics.h"
#include "Voxels.h"
#include <Randomize.hh>
namespace spacerad::engine::transport {
Json buildScores(const State& state) {
    const auto& project=state.project;
    Json result={{"status","completed"},{"targets",Json::array()},{"track_segments",state.segments},
        {"histories",project.events},{"expected_primaries",project.expectedPrimaries()},
        {"geometry_audit",state.geometryAudit},
        {"material_audit",state.materialAudit},
        {"normalization",project.quantity==SpectrumQuantity::Flux?
            "isotropic inward cosine sphere; pi*R_cm^2*omni_flux*duration_s":
            "isotropic inward cosine sphere; pi*R_cm^2*omni_fluence; duration_s is context only"},
        {"scoring","TID=(total_edep-nonionizing_edep)/mass; event-level standard error includes zero histories"}};
    result["random_engine"]=G4Random::getTheEngine()->name();
    for(const auto& entry:state.scores) {
        const auto& v=entry.second;
        double factor=1.602176634e-13/v.mass*project.expectedPrimaries();
        Json row={{"id",entry.first},{"mass_kg",v.mass},{"nonzero_histories",v.ionizing.nonzero},
            {"tid_Gy",v.ionizing.mean*factor},{"tid_rad",v.ionizing.mean*factor*100},
            {"tid_standard_error_Gy",v.ionizing.standardError()*factor},
            {"total_absorbed_Gy",v.total.mean*factor},{"nonionizing_Gy",v.nonionizing.mean*factor},
            {"relative_standard_error",nullptr},{"primary_entry_histories",v.primaryEntryHistories}};
        if(v.ionizing.mean>0) row["relative_standard_error"]=v.ionizing.standardError()/v.ionizing.mean;
        result["targets"].push_back(row);
        auto moments=[](const Moments& m){return Json{{"count",m.count},{"nonzero_histories",m.nonzero},
            {"mean_MeV",m.mean},{"m2_MeV2",m.m2}};};
        result["score_moments"][entry.first]={{"ionizing",moments(v.ionizing)},{"total",moments(v.total)},
            {"nonionizing",moments(v.nonionizing)}};
    }
    const auto& audit=state.sourceAudit;
    result["source_diagnostics"]={{"histories",audit.mu.count},{"mean_position_cos_theta",audit.z.mean},
        {"mean_inward_cos_theta",audit.mu.mean},{"mean_inward_cos_theta_squared",audit.mu2.mean},
        {"inward_cos_theta_bins",audit.muBins},{"species_histories",audit.species},
        {"mean_primary_energy_MeV",audit.energy.mean},
        {"maximum_unit_position_error",audit.maximumPositionError},
        {"maximum_direction_norm_error",audit.maximumDirectionNormError},
        {"maximum_inward_cosine_mapping_error",audit.maximumCosineMappingError},
        {"outward_directions",audit.outwardDirections},
        {"expected_mean_inward_cos_theta",2.0/3.0},{"expected_mean_inward_cos_theta_squared",0.5},
        {"scope","Sampling diagnostics; not an external physics validation"}};
    if(state.voxelPhysical){Json moments=Json::array();
        auto raw=[](const Moments& m){return Json{{"count",m.count},{"nonzero_histories",m.nonzero},{"mean_MeV",m.mean},{"m2_MeV2",m.m2}};};
        for(const auto& cell:state.voxelMoments)moments.push_back({{"ionizing",raw(cell[0])},{"total",raw(cell[1])},{"nonionizing",raw(cell[2])}});
        result["voxel_field"]=makeVoxelField(project,state.voxelAudit,moments,project.events);result["edep_spectra"]=Json::object();auto spec=voxelSpec(project.voxelScoring);
        for(const auto& h:state.edepBins)result["edep_spectra"][h.first]=makeEdepSpectrum(spec.edges,h.second,state.edepOverflow.count(h.first)?state.edepOverflow.at(h.first):0,project.events);
        validateVoxelResult(project,result);
    }return result;
}
}
