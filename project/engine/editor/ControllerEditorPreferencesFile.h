#pragma once
#include "ControllerCanvasLayoutFile.h"

namespace Engine::Editor {
// Local per-project settings, not Controller source, runtime data or history.
// Reuse the held ordinary-directory/leaf IO used by graph layout preferences.
class ControllerEditorPreferencesFile final {
    using IO=ControllerCanvasLayoutFile;using Snapshot=ControllerCanvasLayoutSnapshot;
    static std::filesystem::path Path(const Base::ProjectContext& project){
        IO::Need(project.IsOpen(),"Open a project before using Controller preferences");
        return project.GetRootPath()/L"UserSettings/ControllerLayouts/editor.json";
    }
    static bool Decode(const Snapshot& receipt){
        if(!receipt.exists)return true;
        std::string error;IO::Need(receipt.bytes.size()<=Limit && AnimationSystem::Controller::TextCodec::ValidateJson(receipt.bytes,error),"Invalid/bounded/duplicate Controller preferences JSON");
        const auto value=crude_json::value::parse(receipt.bytes);IO::Fields(value,{"version","autoSaveLayout"});
        IO::Need(value["version"].is_number() && value["version"].get<crude_json::number>()==1,"Unsupported Controller preferences version");
        IO::Need(value["autoSaveLayout"].is_boolean(),"Controller auto-save preference must be boolean");
        return value["autoSaveLayout"].get<crude_json::boolean>();
    }
public:
    static constexpr size_t Limit=4096;
    static bool Load(const Base::ProjectContext& project,bool& enabled,Snapshot& snapshot,std::string& error){
        try{
            const auto path=Path(project);IO::Directories directories(project,false);Snapshot candidate;candidate.path=path;
            if(directories.complete)candidate=IO::Read(path,Limit);
            const bool value=Decode(candidate);snapshot.Swap(candidate);enabled=value;error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
    static bool SaveChecked(const Base::ProjectContext& project,bool enabled,Snapshot& snapshot,std::string& error){
        try{
            const auto path=Path(project);IO::Need(snapshot.path==path,"Reload Controller preferences for this project before saving");
            std::string bytes=enabled?"{\"version\":1,\"autoSaveLayout\":true}\n":"{\"version\":1,\"autoSaveLayout\":false}\n";
            IO::Directories directories(project,true);auto lockPath=path;lockPath+=L".lock";
            IO::Handle lock(CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));IO::Ordinary(lock,false);
            LARGE_INTEGER lockSize{};IO::Need(GetFileSizeEx(lock.value,&lockSize) && lockSize.QuadPart==0,"Controller preferences writer lock must be empty");
            auto current=IO::Read(path,Limit);IO::Need(current.exists==snapshot.exists && current.bytes==snapshot.bytes,"Controller preferences changed externally; reload before saving");
            Decode(current);current.bytes.swap(bytes);current.exists=true;
            if(!Base::AtomicFile::Write(path,current.bytes,Limit,error))return false;
            snapshot.Swap(current);error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
};
}
