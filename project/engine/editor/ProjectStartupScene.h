#pragma once
#include "engine/base/ProjectContext.h"
#include "engine/scene/entity/EntityWorld.h"
#include <fstream>
#include <stdexcept>

namespace Engine::Editor {
// New UI selections are existing, valid generic scenes. Existing legacy/missing
// startup entries are retained when only unrelated launch settings are changed.
inline bool SelectProjectStartupScene(const Engine::Base::ProjectContext& project,
    const std::filesystem::path& selection,std::string& relative,std::string& error,
    Engine::Scene::EntityWorld* outputWorld=nullptr)
{
    try {
        if(!project.IsOpen()) throw std::runtime_error("Open a project before selecting its startup Scene");
        if(selection.empty() || selection.native().find(L'\0')!=std::wstring::npos)
            throw std::runtime_error("Select a saved project Scene");
        const auto absolute=selection.is_absolute() ? selection.lexically_normal() : project.ResolvePath(selection);
        auto component=absolute.begin();
        for(const auto& rootComponent:project.GetRootPath()) {
            if(component==absolute.end() || _wcsicmp(component->c_str(),rootComponent.c_str())!=0)
                throw std::runtime_error("Startup Scene must be inside the project");
            ++component;
        }
        std::filesystem::path candidate;
        for(;component!=absolute.end();++component) candidate/=*component;
        if(candidate.empty()) throw std::runtime_error("Select a Scene file, not the project root");
        for(const auto& component:candidate) if(component==L"..") throw std::runtime_error("Startup Scene must be inside the project");
        const auto text=Engine::Base::PathToUtf8(candidate);
        // Reapply the project's component/path guards without following cwd.
        (void)project.ResolvePath(text);
        Engine::Scene::SceneValue parsed;
        if(!Engine::Scene::SceneDocument::Read(absolute,parsed,error)) return false;
        Engine::Scene::EntityWorld checked;
        if(!checked.Restore(parsed,error)) return false;
        relative=text;
        if(outputWorld) *outputWorld=std::move(checked);
        error.clear();return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}
inline bool SelectProjectStartupScene(const Engine::Base::ProjectContext& project,
    const std::string& selection,std::string& relative,std::string& error)
{
    try {return SelectProjectStartupScene(project,Engine::Base::PathFromUtf8(selection),relative,error);}
    catch(const std::exception& exception) {error=exception.what();return false;}
}
}
