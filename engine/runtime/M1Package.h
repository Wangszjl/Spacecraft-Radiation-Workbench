#pragma once
#include "M1Audit.h"
#include <QString>
namespace spacerad::engine {
Json draftM1PlanFile(const QString& job,const QString& newJson);
Json exportM1Package(const QString& planFile,const QString& newDirectory);
Json verifyM1Package(const QString& directory);
QString renderM1Html(const Json& audit);
}
