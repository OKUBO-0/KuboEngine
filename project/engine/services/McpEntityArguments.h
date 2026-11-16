#pragma once
#include "../scene/core/SceneDocument.h"
#include <cmath>
#include <limits>
#include <string>

namespace Engine::Services {
// Validate at both transport and bridge boundaries before any authored mutation.
inline bool ValidateMcpEntityArguments(const Scene::SceneValue& arguments,bool transform,std::string& error) {
    if(!arguments.is_object()) {error="Entity tool arguments must be an object";return false;}
    if(!arguments.contains("entityId") || !arguments["entityId"].is_number()) {error="entityId must be a positive JSON-safe integer";return false;}
    const auto id=arguments["entityId"].get<double>();
    if(!std::isfinite(id) || id<1 || id>9007199254740991.0 || std::floor(id)!=id) {error="entityId must be a positive JSON-safe integer";return false;}
    bool supplied=false;
    for(const auto& [key,value]:arguments.get<crude_json::object>()) {
        if(key=="entityId") continue;
        if(!transform || (key!="position" && key!="rotation" && key!="scale")) {error="Unknown Entity tool argument";return false;}
        if(!value.is_array() || value.get<crude_json::array>().size()!=3) {error="Transform vectors require exactly three numbers";return false;}
        for(const auto& component:value.get<crude_json::array>()) {
            if(!component.is_number()) {error="Transform vectors require finite float-range numbers";return false;}
            const auto number=component.get<double>();
            if(!std::isfinite(number) || std::abs(number)>std::numeric_limits<float>::max()) {error="Transform vectors require finite float-range numbers";return false;}
        }
        supplied=true;
    }
    if(transform && !supplied) {error="Supply at least one local position, rotation or scale vector";return false;}
    error.clear();return true;
}
inline bool ValidateMcpParentArguments(const Scene::SceneValue& arguments,std::string& error) {
    if(!arguments.is_object()) {error="Parent tool arguments must be an object";return false;}
    Scene::SceneValue entity;entity["entityId"]=arguments.contains("entityId")?arguments["entityId"]:Scene::SceneValue{};
    if(!ValidateMcpEntityArguments(entity,false,error)) return false;
    for(const auto& [key,value]:arguments.get<crude_json::object>()) {
        if(key=="keepWorld") {if(!value.is_boolean()) {error="keepWorld must be boolean";return false;}continue;}
        if(key!="entityId" && key!="parentId") {error="Unknown parent tool argument";return false;}
    }
    if(!arguments.contains("parentId") || !arguments["parentId"].is_number()) {error="parentId must be a JSON-safe integer; 0 means root";return false;}
    const auto parent=arguments["parentId"].get<double>();
    if(!std::isfinite(parent) || parent<0 || parent>9007199254740991.0 || std::floor(parent)!=parent) {error="parentId must be a JSON-safe integer; 0 means root";return false;}
    error.clear();return true;
}
}
