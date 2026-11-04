#pragma once
#include "Project.h"
#include <QString>
namespace spacerad {
// Export an accepted voxel result into a new directory containing JSON, VTI,
// CSV and a size/SHA-256 manifest.
void exportVoxelResult(const Json& acceptedResult,const QString& newDirectory);
}
