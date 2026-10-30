#pragma once
#include "BatchStatistics.h"
#include "ProjectFile.h"
namespace spacerad::engine {
// Runs serial transport workers in bounded deterministic waves. The output
// directory must not exist. cancel.request requests cooperative cancellation.
Json runBatchJob(const LoadedProject&,const BatchOptions&,const QString& directory,const Json& runtime,const QString& externalCancel={});
Json resumeBatchJob(const QString& source,const BatchOptions&,const QString& directory,const Json& runtime);
Json replayBatchJob(const LoadedProject&,const BatchOptions&,const std::vector<int>& indices,const QString& directory,const Json& runtime);
}
