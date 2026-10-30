#pragma once
#include "spacerad/Authoring.h"
#include <optional>
class QWidget;
namespace spacerad {
std::optional<Project> editPartDialog(QWidget*,const Project&,const Volume&,const std::string& replaceId={});
}
