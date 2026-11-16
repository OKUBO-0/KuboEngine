#pragma once
#include "engine/base/ProjectContext.h"
#include "engine/scene/entity/EntityWorld.h"
#include <stdexcept>

namespace Engine::Editor {
inline bool SaveProjectSceneAs(const Engine::Base::ProjectContext& project,const Engine::Scene::EntityWorld& world,
    const std::filesystem::path& selection,bool overwriteApproved,std::filesystem::path& savedPath,std::string& error)
{
    try {
        if(!project.IsOpen() || selection.empty() || selection.native().find(L'\0')!=std::wstring::npos)
            throw std::runtime_error("Select a Scene save path in an open project");
        const auto absolute=selection.is_absolute() ? selection.lexically_normal() : project.ResolvePath(selection);
        auto component=absolute.begin();
        for(const auto& root:project.GetRootPath()) {
            if(component==absolute.end() || _wcsicmp(component->c_str(),root.c_str())!=0)
                throw std::runtime_error("Scene save path must be inside the active project");
            ++component;
        }
        std::filesystem::path relative;
        for(;component!=absolute.end();++component) {
            const auto& name=component->native();
            if(name.empty() || name==L".." || name.back()==L'.' || name.back()==L' ' || name.find_first_of(L":<>\"|?*")!=std::wstring::npos)
                throw std::runtime_error("Invalid Scene save path component");
            for(const wchar_t character:name) if(character<32) throw std::runtime_error("Control character in Scene save path");
            auto device=name.substr(0,name.find(L'.'));
            for(auto& character:device) if(character>=L'a' && character<=L'z') character-=L'a'-L'A';
            if(device==L"CON" || device==L"PRN" || device==L"AUX" || device==L"NUL" || device==L"CONIN$" || device==L"CONOUT$" ||
                (device.size()==4 && (device.starts_with(L"COM") || device.starts_with(L"LPT")) &&
                    ((device[3]>=L'0' && device[3]<=L'9') || device[3]==L'\u00b9' || device[3]==L'\u00b2' || device[3]==L'\u00b3')))
                throw std::runtime_error("Reserved device name in Scene save path");
            relative/=*component;
        }
        if(relative.empty() || (_wcsicmp(absolute.extension().c_str(),L".scene")!=0 && _wcsicmp(absolute.extension().c_str(),L".json")!=0))
            throw std::runtime_error("Scene filename must end in .scene or .json");
        const auto utf8=Engine::Base::PathToUtf8(relative);
        if(utf8.size()>4096) throw std::runtime_error("Scene save path exceeds 4096 UTF-8 bytes");
        (void)project.ResolvePath(utf8);
        Engine::Scene::EntityWorld checked;
        if(!checked.Restore(world.Capture(),error)) return false;
        if(std::filesystem::exists(absolute)) {
            if(!overwriteApproved) throw std::runtime_error("Scene already exists; explicit overwrite approval is required");
            Engine::Scene::EntityWorld existing;
            if(!existing.Load(absolute,error)) throw std::runtime_error("Refusing to overwrite a file that is not a valid generic Scene: "+error);
        }
        if(!world.Save(absolute,error,overwriteApproved)) return false;
        savedPath=absolute;error.clear();return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}
}
