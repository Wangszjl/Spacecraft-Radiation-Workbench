#pragma once
#include "spacerad/Project.h"
#include <QDialog>
namespace spacerad {
void showVoxelDialog(QWidget* parent,const Project& project,const Json& acceptedResult);
}
