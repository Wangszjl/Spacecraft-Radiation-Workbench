#pragma once
#include "TriangleMeshExchange.h"
#include "MeshSpatialAudit.h"
#include <QStringList>
namespace spacerad::engine {
struct StepConversionReview { Json audit;QStringList meshPaths;std::vector<MeshAssemblyPart> relationParts; };
// Verify frozen package bytes and recompute every mapped mesh review. No CAD
// backend is executed and no original assembly/material fidelity is inferred.
StepConversionReview inspectStepConversionFile(const QString& manifestPath);
struct StepRelationReview { Json report;std::vector<MeshAssemblyPart> parts;QStringList additionalPaths; };
StepRelationReview inspectStepRelationsFile(const QString& manifestPath,bool leafBvhRecheck=false);
StepRelationReview inspectStepContactEvidenceFile(const QString& manifestPath);
// Frozen local numerical observations only; never executes the CAD backend.
StepRelationReview inspectStepBrepWitnessFile(const QString& manifestPath,const QString& receiptPath);
StepRelationReview inspectStepBrepPairsFile(const QString& manifestPath,const QString& receiptPath);
}
