#pragma once
#include "BatchArchive.h"
namespace spacerad::engine {
// Export an immutable terminal job snapshot as offline HTML + JSON + evidence.
// A GUI job root automatically resolves its batches subdirectory when present.
Json exportReproReport(const QString& jobDirectory,const QString& newDirectory);
Json verifyReproReport(const QString& directory);
struct ReportReplayInput {
    LoadedProject input;BatchOptions options;std::vector<int> indices;
    Json report,expected,runtime;std::string workerSha;
};
ReportReplayInput loadReportReplay(const QString& directory);
Json compareReportReplay(const Json& expected,const Json& actual);
}
