#pragma once
#include "spacerad/PartitionStudy.h"
#include <QWidget>
namespace spacerad {
void showPartitionDialog(QWidget*,const Project&,const RuntimeOptions&,const QString& archive={});
}
