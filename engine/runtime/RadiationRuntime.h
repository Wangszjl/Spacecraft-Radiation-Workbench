#pragma once
#include "RadiationProject.h"
#include <QByteArray>
#include <QString>

namespace spacerad::engine {
Json auditRuntime();
Json addProvenance(Json result, const Project& project, const QByteArray& input,
                   const QString& started, const Json& runtime);
void writeResult(const QString& path, const Json& result);
}
