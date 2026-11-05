#pragma once
#include "spacerad/Project.h"
#include <QString>
class QWidget;
namespace spacerad {
void showImportanceDialog(QWidget*,const Project&,const Json&,const QString& target={});
}
