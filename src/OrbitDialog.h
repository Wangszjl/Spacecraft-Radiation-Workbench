#pragma once
#include "spacerad/OrbitEnvironment.h"
#include <QWidget>
#include <functional>
namespace spacerad {
void showOrbitDialog(QWidget*,const Project&,std::function<void(const Project&)> apply);
}
