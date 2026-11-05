#pragma once
#include "Project.h"

namespace spacerad {
// Reconstruct observed region contributions; passing is an MC screening result,
// never a guarantee on unseen events or a physical validation.
Json importanceReview(const Project&,const Json& result,
                      int minNonzeroPerRegion=20,double maxTotalRse=0.2);
}
