#pragma once
#include "spacerad/Project.h"
#include <optional>
class QWidget;
namespace spacerad {
std::optional<Project> editSpectrumDialog(QWidget*, const Project&, std::size_t index);
}
