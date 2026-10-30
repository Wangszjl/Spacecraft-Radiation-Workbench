#pragma once

#include "RadiationProject.h"
#include <QString>

namespace spacerad::engine {

// Serialize a validated scdat.radiation-voxel-field/1 field. The CSV and VTI
// represent the same native, uniform-mass voxel cells; neither interpolates.
QString voxelVti(const Json& field);
QString voxelCsv(const Json& field);

// Atomically publish field.json, field.vti, field.csv and a SHA-256 manifest
// into a directory that does not already exist.
void exportVoxelFieldFiles(const Json& result, const QString& newDirectory);

}
