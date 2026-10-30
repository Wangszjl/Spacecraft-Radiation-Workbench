#pragma once
#include "TriangleMeshExchange.h"
namespace spacerad::engine {
struct ConvexSeparationBudget {
    size_t axesPerPair=4096,candidatesPerPair=50000,vertexProjections=1000000;
};
// Sufficient witnesses for already-qualified closed convex parts. Never an
// overlap classification or a general transport authorization. All arithmetic
// bounds use outward-rounded operations; exhaustion is an explicit blocker.
Json auditConvexSeparation(const std::vector<TriangleMeshReview>&,
    const ConvexSeparationBudget& = {});
Json auditConvexSourceEnclosure(const std::vector<TriangleMeshReview>&,double radiusMm);
}
