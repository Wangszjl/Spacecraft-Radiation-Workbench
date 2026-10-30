#pragma once
#include "BatchStatistics.h"
#include "ProjectFile.h"
#include <QLockFile>
#include <memory>
namespace spacerad::engine {
struct ArchivedBatch {Json record,result;QByteArray projectBytes,resultBytes,logBytes;};
struct BatchArchive {
    QString directory;LoadedProject input;BatchOptions options;Json manifest,identity;
    std::vector<ArchivedBatch> completed;
    // Hold the cooperative source lock through validation and copying.
    std::unique_ptr<QLockFile> lock;
};
std::string archiveSha(const QByteArray&);
BatchArchive loadBatchArchive(const QString& directory);
Json batchArchiveReport(const BatchArchive&,const Json& runtime,const std::string& workerSha);
LoadedProject prepareBatchResume(const BatchArchive&,const BatchOptions&,const Json& runtime,const std::string& workerSha);
Json compareProjectFiles(const QString& left,const QString& right);
}
