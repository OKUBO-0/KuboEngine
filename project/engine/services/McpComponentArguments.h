#pragma once
#include "McpEntityArguments.h"
#include <numbers>
#include <utility>
#include <filesystem>
#include "../script/ScriptProperty.h"
#include "../scene/entity/RenderComponents.h"
#include "../scene/entity/UIComponents.h"

namespace Engine::Services {
// One source of field types/ranges for both advertised schema and transport/IPC validation.
inline Scene::SceneValue McpComponentPatchSchema(const std::string& component) {
    using Value=Scene::SceneValue;
    Value schema;schema["type"]="object";schema["additionalProperties"]=false;schema["minProperties"]=1.0;
    schema["properties"]=crude_json::object{};
    const auto boolean=[&](const char* field){schema["properties"][field]["type"]="boolean";};
    const auto number=[&](const char* field,double minimum,double maximum){
        auto& value=schema["properties"][field];value["type"]="number";value["minimum"]=minimum;value["maximum"]=maximum;
    };
    const auto vector=[&](const char* field,double minimum){
        auto& value=schema["properties"][field];value["type"]="array";value["minItems"]=3.0;value["maxItems"]=3.0;
        value["items"]["type"]="number";value["items"]["minimum"]=minimum;value["items"]["maximum"]=double(std::numeric_limits<float>::max());
    };
    const auto maximum=double(std::numeric_limits<float>::max());
    const auto positive=double(std::numeric_limits<float>::denorm_min());
    if(component!="model" && component!="material")boolean("enabled");
    if(component=="camera") {
        boolean("primary");number("fovY",positive,double(std::nextafter(std::numbers::pi_v<float>,0.0f)));number("nearClip",positive,maximum);
        number("farClip",positive,maximum);number("orthographicHeight",positive,maximum);
        schema["properties"]["projection"]["type"]="string";
        schema["properties"]["projection"]["enum"]=crude_json::array{"perspective","orthographic"};
    } else if(component=="collider") {
        boolean("trigger");vector("halfExtents",0.0);number("friction",0.0,1.0);number("restitution",0.0,1.0);
    } else if(component=="rigidBody") {
        boolean("dynamic");boolean("gravity");vector("velocity",-maximum);number("mass",positive,maximum);
    } else if(component=="audio") {
        boolean("loop");boolean("playOnAwake");number("volume",0.0,1.0);
    } else if(component=="light") {
        auto& type=schema["properties"]["type"];type["type"]="string";type["enum"]=crude_json::array{"directional","point","spot"};
        number("intensity",0,1000000);number("range",double(0.0001f),1000000);number("decay",0,16);
        number("innerAngle",0,double(Scene::MaximumLightAngle));number("outerAngle",double(0.0001f),double(Scene::MaximumLightAngle));
    } else if(component=="material") {
        boolean("lighting");number("shininess",1,1024);number("roughness",0,1);number("reflectionStrength",0,1);number("textureInfluence",0,1);
    } else if(component=="canvas" || component=="sprite") {
        number("sortOrder",-32768,32767);schema["properties"]["sortOrder"]["type"]="integer";
        const auto pair=[&](const char* field,double lo,double hi){auto& v=schema["properties"][field];v["type"]="array";v["minItems"]=v["maxItems"]=2.0;v["items"]["type"]="number";v["items"]["minimum"]=lo;v["items"]["maximum"]=hi;};
        if(component=="canvas"){
            pair("referenceSize",1,16384);auto& mode=schema["properties"]["scaleMode"];mode["type"]="string";mode["enum"]=crude_json::array{"constantPixels","scaleWithOutput"};
        }else{pair("size",0,16384);pair("pivot",-16,16);boolean("flipX");boolean("flipY");}
    }
    if(component=="light" || component=="material" || component=="sprite") {
        auto& color=schema["properties"]["color"];color["type"]="array";
        color["minItems"]=color["maxItems"]=component=="light"?3.0:4.0;
        color["items"]["type"]="number";color["items"]["minimum"]=0.0;color["items"]["maximum"]=1.0;
    }
    if(component=="model" || component=="script" || component=="audio" || component=="sprite") {
        schema["properties"]["asset"]["type"]="string";schema["properties"]["asset"]["maxLength"]=4096.0;
        Value guid;guid["type"]="string";guid["pattern"]="^([0-9a-f]{16}|[0-9a-f]{32})$";
        Value cleared;cleared["type"]="null";
        schema["properties"]["guid"]["anyOf"]=crude_json::array{guid,cleared};
    }
    if(component=="script") {
        auto& properties=schema["properties"]["properties"];properties["type"]="object";properties["maxProperties"]=64.0;
        properties["propertyNames"]["type"]="string";properties["propertyNames"]["minLength"]=1.0;properties["propertyNames"]["maxLength"]=128.0;
        Value numeric;numeric["type"]="number";numeric["minimum"]=-maximum;numeric["maximum"]=maximum;
        Value text;text["type"]="string";text["maxLength"]=1024.0;
        Value flag;flag["type"]="boolean";
        properties["additionalProperties"]["anyOf"]=crude_json::array{numeric,text,flag};
    }
    return schema;
}
inline bool ValidMcpAssetPath(const std::string& text) {
    if(!Scripting::ValidScriptText(text,4096) || text.find(':')!=std::string::npos)return false;
    try {
        const auto path=std::filesystem::path(std::u8string(text.begin(),text.end()));
        if(path.has_root_path())return false;
        for(const auto& part:path)if(part=="..")return false;
        return true;
    }catch(const std::exception&){return false;}
}
inline bool ValidateMcpComponentArguments(const Scene::SceneValue& arguments,std::string& error) {
    using Value=Scene::SceneValue;
    if(!arguments.is_object()){error="Component arguments must be an object";return false;}
    Value entity;entity["entityId"]=arguments.contains("entityId")?arguments["entityId"]:Value{};
    if(!ValidateMcpEntityArguments(entity,false,error))return false;
    for(const auto& [key,value]:arguments.get<crude_json::object>())
        if(key!="entityId" && key!="component" && key!="patch"){error="Unknown Component argument";return false;}
    if(!arguments.contains("component") || !arguments["component"].is_string()){error="component must name a supported Component";return false;}
    const auto& component=arguments["component"].get<std::string>();
    if(component!="camera" && component!="collider" && component!="rigidBody" && component!="model" && component!="script" && component!="audio" && component!="light" && component!="material" && component!="canvas" && component!="sprite"){error="Unsupported Component";return false;}
    if(!arguments.contains("patch")){error="patch is required (null removes the Component)";return false;}
    const auto& patch=arguments["patch"];
    if(patch.is_null()){error.clear();return true;}
    if(!patch.is_object() || patch.get<crude_json::object>().empty()){error="patch must be a nonempty object or null";return false;}
    const auto schema=McpComponentPatchSchema(component);
    const auto validNumber=[](const Value& number,const Value& definition){
        if(!number.is_number())return false;
        const auto value=number.get<double>();
        return std::isfinite(value) && value>=definition["minimum"].get<double>() && value<=definition["maximum"].get<double>();
    };
    for(const auto& [field,value]:patch.get<crude_json::object>()) {
        if(!schema["properties"].contains(field)){error="Unknown Component patch field: "+field;return false;}
        if(field=="guid") {
            if(value.is_null())continue;
            if(!value.is_string()){error="guid must be a lowercase 16/32-digit identity or null";return false;}
            const auto& guid=value.get<std::string>();
            if((guid.size()!=16 && guid.size()!=32) || guid.find_first_not_of("0123456789abcdef")!=std::string::npos){error="Invalid asset GUID";return false;}
            continue;
        }
        if(field=="properties") {
            if(!value.is_object() || value.get<crude_json::object>().size()>64){error="Script properties must be an object with at most 64 scalar values";return false;}
            for(const auto& [name,property]:value.get<crude_json::object>()) {
                if(!Scripting::ValidScriptText(name,128,true)){error="Invalid Script property name";return false;}
                bool valid=property.is_boolean();
                if(property.is_number())valid=Scripting::ValidScriptProperty(property.get<double>());
                else if(property.is_string())valid=Scripting::ValidScriptProperty(property.get<std::string>());
                if(!valid){error="Script properties require bounded finite number/boolean/UTF-8 string values";return false;}
            }
            continue;
        }
        const auto& definition=schema["properties"][field];const auto& type=definition["type"].get<std::string>();
        bool valid=false;
        if(type=="boolean")valid=value.is_boolean();
        else if(type=="number")valid=validNumber(value,definition);
        else if(type=="integer")valid=validNumber(value,definition) && std::floor(value.get<double>())==value.get<double>();
        else if(type=="string" && value.is_string()) {
            if(field=="asset")valid=component=="sprite"?Scene::UIComponentDetail::AssetPath(value.get<std::string>()):ValidMcpAssetPath(value.get<std::string>());
            else if(definition.contains("enum"))for(const auto& allowed:definition["enum"].get<crude_json::array>())if(value.get<std::string>()==allowed.get<std::string>())valid=true;
        }
        else if(type=="array" && value.is_array() && value.get<crude_json::array>().size()==size_t(definition["minItems"].get<double>())) {
            valid=true;for(const auto& number:value.get<crude_json::array>())if(!validNumber(number,definition["items"]))valid=false;
        }
        if(!valid){error="Invalid Component patch field: "+field;return false;}
    }
    error.clear();return true;
}
inline Scene::SceneValue McpComponentToolSchema() {
    using Value=Scene::SceneValue;
    Value schema;schema["type"]="object";schema["additionalProperties"]=false;
    schema["required"]=crude_json::array{"entityId","component","patch"};
    schema["properties"]["entityId"]["type"]="integer";
    schema["properties"]["entityId"]["minimum"]=1.0;schema["properties"]["entityId"]["maximum"]=9007199254740991.0;
    schema["properties"]["component"]["type"]="string";
    schema["properties"]["component"]["enum"]=crude_json::array{"camera","collider","rigidBody","model","script","audio","light","material","canvas","sprite"};
    schema["properties"]["patch"]=crude_json::object{};
    schema["oneOf"]=crude_json::array{};
    for(const char* name:{"camera","collider","rigidBody","model","script","audio","light","material","canvas","sprite"}) {
        Value branch;branch["properties"]["component"]["enum"]=crude_json::array{name};
        Value removed;removed["type"]="null";
        branch["properties"]["patch"]["anyOf"]=crude_json::array{McpComponentPatchSchema(name),removed};
        schema["oneOf"].push_back(std::move(branch));
    }
    return schema;
}
}
