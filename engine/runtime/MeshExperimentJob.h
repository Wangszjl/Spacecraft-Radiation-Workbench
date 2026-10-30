#pragma once
#include "TriangleMeshExchange.h"
namespace spacerad::engine {
struct MeshExperimentJob {
    Project source; // Source/material configuration, NEVER transport geometry. v3 uses validation proxies.
    TriangleMeshReview mesh;
    Json document;
    QByteArray bytes;
    bool analyticReference=false;
    std::vector<TriangleMeshReview> parts;
    Json separationAudit;
    bool assembly=false;
    bool convexSeparation=false;
    bool singleSurface=false; // Explicit v4: one connected closed surface, concavity allowed.
    bool starFanAssembly=false; // Explicit v6: certified covers, original physical solids.
    bool nonconvexAssembly=false; // Explicit v5: single surfaces, separated vertex hulls.
    std::vector<std::string> referenceProfiles; // Derived CLI-only reference geometry, not input approval.
    Json sourceEnclosure;
};
MeshExperimentJob loadMeshExperiment(const QString&,bool allowBlockedInspection=false);
MeshExperimentJob parseMeshExperiment(const QByteArray&,bool allowBlockedInspection=false);
Json meshExperimentDemo(const std::string& particle="e-");
Json meshAssemblyExperimentDemo(const std::string& particle="e-",bool shielded=true);
Json meshRotatedExperimentDemo(const std::string& particle="e-");
// Rechecks a single-solid conversion/2 with an adaptive volume reference.
// Returns {job, step_conversion_review}; records explicit whole-body dosimetry.
Json prepareStepMeshExperiment(const QString& manifest,const std::string& particle="e-");
}
