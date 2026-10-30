#pragma once
#include "BatchStatistics.h"
namespace spacerad::engine {
struct ResponseOptions {
    std::vector<std::string> particles;
    std::vector<double> edges;
    int historiesPerCase=2000,historiesPerBatch=1000,maxParallel=2;
    static ResponseOptions fromJson(const Json&);
    Json toJson() const;
    BatchOptions batchOptions() const;
};
struct ResponseCase {int index;std::string particle;double low,high;Project project;};
std::vector<ResponseCase> planResponseCases(const Project&,const ResponseOptions&);
Json responseDescriptor(const Project&,const ResponseOptions&);
Json responseCacheIdentity(const Project&,const ResponseOptions&,const std::string& workerSha,const Json& runtime);
Json makeResponseMatrix(const Project&,const ResponseOptions&,const std::vector<Json>& results);
Json applyResponseMatrix(const Project&,const ResponseOptions&,const Json& matrix);
}
