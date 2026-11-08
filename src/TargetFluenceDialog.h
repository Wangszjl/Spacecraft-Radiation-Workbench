#pragma once
#include <spacerad/Project.h>
#include <QString>
#include <optional>
#include <utility>
class QWidget;
namespace spacerad {
std::optional<std::pair<Json,QString>> configureTargetFluence(QWidget*,const Project&,const QString&);
void showTargetFluenceDialog(QWidget*,const QString&);
}
