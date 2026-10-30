#pragma once
#include "ResponseMatrix.h"
#include "ProjectFile.h"
namespace spacerad::engine {
struct ResponseCache {QString directory;LoadedProject input;ResponseOptions options;Json manifest,matrix;};
QString responseCasePath(int);
QByteArray responseRead(const QString& path);
Json responseFileInventory(const QString& root);
ResponseCache loadResponseCache(const QString& directory);
Json applyResponseCache(const LoadedProject&,const QString& entry,const QString& output,const std::string& workerSha,const Json& runtime);
}
