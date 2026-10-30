#pragma once
#include "RadiationProject.h"
namespace spacerad::engine {
struct GeometryModel {
    std::vector<Volume> volumes;
    std::vector<Assembly> assemblies;
    Json origins = Json::object(); // generated ID -> instance/template/local ID and kind
};
GeometryModel expandInstances(const Project& project);
Json templateToJson(const ComponentTemplate& component);
ComponentTemplate templateFromJson(const Json& json);
Json instanceToJson(const ComponentInstance& instance);
ComponentInstance instanceFromJson(const Json& json);
// Capture authored descendants in the selected assembly's coordinate frame.
// Existing instance descendants must be handled separately, not silently omitted.
ComponentTemplate captureAssembly(const Project& project,const std::string& assemblyId,const std::string& templateId);
}
