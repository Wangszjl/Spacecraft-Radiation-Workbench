#pragma once
#include "spacerad/Project.h"
#include <optional>
class QWidget;
namespace spacerad {
std::optional<Project> editSourceSamplingDialog(QWidget*,const Project&);
}
