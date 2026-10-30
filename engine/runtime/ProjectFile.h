#pragma once
#include "RadiationProject.h"
#include <QByteArray>
#include <QString>
namespace spacerad::engine {
struct LoadedProject { Project project; QByteArray bytes; std::string inputSchema; };
LoadedProject loadProjectFile(const QString& path,bool refreshMaterialSnapshot=false);
Spectrum loadSpectrumCsv(const QString& path,const std::string& particle,bool perSteradian,SpectrumQuantity quantity,bool integral=false);
Json sourceFileIdentity(const QString& path,const QByteArray& bytes,bool embed=false);
void saveProjectFile(const QString& path,const Project& project,bool allowOverwrite=false);
}
