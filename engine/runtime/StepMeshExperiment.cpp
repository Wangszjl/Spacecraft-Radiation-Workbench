#include "MeshExperimentJob.h"
#include "StepConversionReview.h"
#include "ProjectFile.h"
#include <QCryptographicHash>
#include <QFile>
#include <cmath>
#include <stdexcept>
namespace spacerad::engine {
Json prepareStepMeshExperiment(const QString& manifest,const std::string& particle){
    auto conversion=inspectStepConversionFile(manifest);const auto& audit=conversion.audit;
    if(audit.at("schema")!="scdat.step-conversion-review/2"||audit.at("volume_reference_status")!="numerically_qualified"||audit.at("parts").size()!=1||conversion.meshPaths.size()!=1||audit.at("parts")[0].at("geometry_gate_passed")!=true)throw std::runtime_error("STEP experiment requires one mapped, closed, reviewed solid and conversion/2 adaptive volume reference");
    QFile f(conversion.meshPaths[0]);if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024)throw std::runtime_error("STEP mesh outside experimental input budget");auto original=f.readAll();auto mesh=inspectTriangleMeshBytes(original,f.fileName(),true);if(mesh.report.at("geometry_gate_passed")!=true)throw std::runtime_error("STEP leaf-BVH audit incomplete or rejected");
    auto job=meshExperimentDemo(particle);job["schema"]="scdat.mesh-experiment/4";job["mesh"]=mesh.bundle;
    // Explicit virtual whole-body dosimeter: preserve geometry/material and ID.
    job["mesh"]["part"]["sensitive"]=true;
    double maximum=0;for(auto point:mesh.mesh.points){auto w=mesh.placement.apply(point);double squared=0;for(double x:w)squared+=x*x;maximum=std::max(maximum,std::sqrt(squared));}
    job["run"]["source_radius_mm"]=std::max(1.,(maximum+1.)/.95);job["run"]["histories"]=40000;
    auto embedded=QByteArray::fromStdString(job.at("mesh").dump());double reference=audit.at("volume_reference").at("reference_volume_mm3");
    job["cad_provenance"]={{"schema","scdat.step-mesh-provenance/1"},{"conversion_sha256",audit.at("manifest").at("sha256")},{"source_sha256",audit.at("source").at("sha256")},{"inventory_sha256",audit.at("inventory").at("sha256")},{"material_map_sha256",audit.at("material_map").at("sha256")},{"original_mesh_sha256",sourceFileIdentity(f.fileName(),original).at("sha256")},{"embedded_mesh_sha256",QCryptographicHash::hash(embedded,QCryptographicHash::Sha256).toHex().toStdString()},{"part_id",mesh.report.at("id")},{"material",mesh.report.at("material")},{"original_sensitive",mesh.report.at("sensitive")},{"scoring_override","whole_body_dosimeter"},{"source_solid_count",1},{"reference_status","numerically_qualified"},{"reference_volume_mm3",reference},{"mesh_to_reference_relative_error",std::abs(mesh.report.at("quality").at("signed_volume_mm3").get<double>()/reference-1)},{"mapping_basis",audit.at("material_map").at("basis")},{"provenance_authenticated",false},{"source_fidelity_certified",false}};
    parseMeshExperiment(QByteArray::fromStdString(job.dump()));
    if(inspectStepConversionFile(manifest).audit!=audit)throw std::runtime_error("STEP package changed during preparation");
    return {{"schema","scdat.step-mesh-preparation/1"},{"job",job},{"step_conversion_review",audit},{"runtime_geometry_executed",false},{"transport_allowed",false},{"scope","One source solid, unchanged vertices/facets/material/placement; explicit whole-body dosimeter. Observational hashes, no authenticated CAD fidelity or assembly approval."}};
}
}
