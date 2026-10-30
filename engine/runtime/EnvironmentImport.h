#pragma once
#include "ProjectFile.h"
namespace spacerad::engine {
// Native tabular format only; selecting a row is never an implicit orbit average.
Json inspectSpenvis(const QByteArray& bytes);
struct EnvironmentImport {
    Spectrum spectrum;
    SpectrumQuantity quantity;
    Json metadata=defaultEnvironmentMetadata();
    Json mapping;
};
EnvironmentImport parseSpenvis(const QByteArray& bytes,const Json& options);
EnvironmentImport loadSpenvis(const QString& path,const Json& options);
Project applyEnvironmentImport(const Project& base,const EnvironmentImport& imported,bool synthetic);
}
