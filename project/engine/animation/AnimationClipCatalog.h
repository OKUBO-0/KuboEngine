#pragma once
#include "../script/ScriptProperty.h"
#include <algorithm>
#include <vector>

namespace Engine::AnimationSystem {
// Read-only names from loaded animation data, not an asset import or a clock.
// Exact UTF-8 byte sorting; default alias is first, other names are case-sensitive.
template<class Clips,class Clip>
std::vector<std::string> CollectAnimationClipNames(const Clips& clips,const Clip& fallback)
{
    std::vector<std::string> names;
    for(const auto& [name,clip]:clips) {
        if(Engine::Scripting::ValidScriptText(name,256,true) && !clip.nodeAnimations.empty() &&
            std::isfinite(clip.duration) && clip.duration>=0)names.push_back(name);
    }
    if(clips.find("default")==clips.end() && !fallback.nodeAnimations.empty() &&
        std::isfinite(fallback.duration) && fallback.duration>=0)names.emplace_back("default");
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    const auto found=std::find(names.begin(),names.end(),"default");
    if(found!=names.end())std::rotate(names.begin(),found,found+1);
    return names;
}
}
