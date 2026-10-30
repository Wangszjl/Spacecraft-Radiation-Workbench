#pragma once
#include "Project.h"
namespace spacerad {
// Immutable edits: every returned project passes the existing geometry/source
// gates. Material names remain declarations until resolved by the Worker.
Volume suggestPart(const Project&,const std::string& shape,bool sensitive);
Project addPart(const Project&,const Volume&);
Project replacePart(const Project&,const std::string& authoredId,const Volume&);
Project removePart(const Project&,const std::string& authoredId);
}
