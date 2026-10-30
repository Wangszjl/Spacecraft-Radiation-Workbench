#pragma once
#include "MeshExperimentJob.h"
#include <QStringList>
namespace spacerad::engine {
// Three same-source conversion/2 levels, sorted coarse -> fine. Optional local
// CAD observations are bound and their point-to-mesh distances recomputed;
// this never executes/authenticates the CAD backend or certifies convergence.
Json inspectStepMeshStudy(const QStringList& manifests,const QString& samplingReport={});
// Recompute ionizing event statistics and normalization, including zero events.
// Goals apply to this completed fixed-N run, not geometry/physics convergence.
Json assessMeshExperimentStatistics(const MeshExperimentJob&,const Json& result,
    double targetRelativeError=.05,int minNonzeroHistories=30);
}
