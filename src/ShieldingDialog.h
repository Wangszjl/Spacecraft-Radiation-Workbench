#pragma once
#include "spacerad/ShieldingScan.h"
#include <QWidget>
namespace spacerad {
void showShieldingDialog(QWidget* parent,const Project&,const RuntimeOptions&,const QString& archive={});
}
