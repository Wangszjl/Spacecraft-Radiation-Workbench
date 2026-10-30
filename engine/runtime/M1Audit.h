#pragma once
#include "RadiationProject.h"
namespace spacerad::engine {
struct M1Run {Project project;Json result,report;};
Json validateM1Plan(const Json&);
Json draftM1Plan(const Project&,const std::string& jobDirectory);
// Callers first validate original transport/geometry and bind attachment bytes.
Json evaluateM1(const Json& plan,const std::vector<M1Run>& runs,const Json& attachmentIdentities);
}
