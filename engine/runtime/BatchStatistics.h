#pragma once
#include "RadiationProject.h"
#include <vector>
namespace spacerad::engine {
struct BatchOptions {
    int historiesPerBatch=10000,maxHistories=100000,maxParallel=2;
    double maxSeconds=0,targetRelativeError=.1;
    int minNonzeroHistories=30,minCompletedBatches=2;
    static BatchOptions fromJson(const Json&);
    Json toJson() const;
};
struct BatchEntry { int index,seed,firstEvent,histories; };
std::vector<BatchEntry> planBatches(const Project&,const BatchOptions&);
Json batchPlanJson(const std::vector<BatchEntry>&);
// Complete event moments, including zeros, in MeV per sampled primary.
// Exposure and mass normalization are applied once after pooling histories.
Json mergeBatchResults(const Project&,const std::vector<Json>& results);
Json batchConvergence(const Json& merged,const BatchOptions&,int completedBatches);
}
