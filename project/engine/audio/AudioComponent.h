#pragma once
#include "../script/ScriptProperty.h"
#include <filesystem>

namespace Engine::AudioSystem {
// Authored state only; device handles/playback positions never enter Scene JSON.
struct AudioComponent {
    std::string assetPath,assetGuid;
    bool enabled=true,loop=false,playOnAwake=false;
    float volume=1;
    bool IsValid() const {
        if(!std::isfinite(volume) || volume<0 || volume>1 || !Scripting::ValidScriptText(assetPath,4096)) return false;
        if(!assetGuid.empty()) {
            if(assetGuid.size()!=16 && assetGuid.size()!=32) return false;
            for(char value:assetGuid) if(!((value>='0'&&value<='9') || (value>='a'&&value<='f'))) return false;
        }
        if(assetPath.empty()) return true; // Allows authoring an unassigned source.
        if(assetPath.find(':')!=std::string::npos) return false;
        try {
            const auto path=std::filesystem::path(std::u8string(assetPath.begin(),assetPath.end()));
            if(path.has_root_name() || path.has_root_directory()) return false;
            for(const auto& part:path) if(part=="..") return false;
            return true;
        } catch(...) {return false;}
    }
};
}
