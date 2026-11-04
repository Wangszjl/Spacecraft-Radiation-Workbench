#include "spacerad/VoxelExport.h"
#include "runtime/VoxelExport.h"
namespace spacerad {
void exportVoxelResult(const Json& result,const QString& directory){engine::exportVoxelFieldFiles(result,directory);}
}
