#pragma once
#include "../Geometry.h"
#include <QByteArray>
#include <QString>
namespace spacerad::engine {
struct ViewPropertiesContext {
    Project project;
    CompiledGeometry geometry;
    bool diagnostic=false;
    Json materialSnapshot=nullptr,geometryAudit=nullptr,targets=nullptr;
    Json inputIdentity=nullptr,resultIdentity=nullptr,worker=nullptr;
    std::string engineVersion;
    std::string resultStatus;
};
ViewPropertiesContext makeViewPropertiesContext(const Project&,bool diagnostic=false);
void bindViewPropertiesResult(ViewPropertiesContext&,const QByteArray& resultBytes,const Json& inputIdentity,const QString& resultPath);
Json viewSelectionProperties(const ViewPropertiesContext&,const std::string& id);
}
