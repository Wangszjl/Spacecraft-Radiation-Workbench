#pragma once
#include "Project.h"
#include <QString>
namespace spacerad {
class ArchivedRun;
struct DeviceResponseCurve {
    std::string deviceId,particle,reference;
    std::vector<double> energy_MeV,values;
};
struct DeviceResponseTable {
    std::string metric,responseUnit,angularBasis;
    std::vector<DeviceResponseCurve> curves;
    Json sourceFile=nullptr;
    void validate() const;
    Json toJson() const;
};
DeviceResponseTable loadDeviceResponseCsv(const QString& path);
Json foldDeviceResponseTable(const Project&,const DeviceResponseTable&);
// Uses a verified volume-averaged target fluence archive, not the source spectrum.
Json foldTargetFluenceResponse(const ArchivedRun&,const DeviceResponseTable&);
QByteArray deviceResponseCsv(const Json&);
Json runDeviceResponseStudy(const Project&,const QByteArray& responseCsv,const QString& sourceName);
Json saveDeviceResponseStudy(const Project&,const QString& responseCsvPath,const QString& newStudyDirectory);
Json loadDeviceResponseStudy(const QString& directory);
}
