#pragma once
#include "ShieldingScan.h"
#include "ProjectFile.h"
namespace spacerad::engine {
Json runShieldingScan(const LoadedProject&,const ShieldingScanOptions&,const QString& newDirectory,const Json& runtime);
Json verifyShieldingScan(const QString& directory);
}
