#pragma once
#include "ModelReference.h"
namespace Engine::Assets {
inline std::filesystem::path ResolveAudioReference(const AudioSystem::AudioComponent& audio,
    const AssetRegistry& registry,const std::filesystem::path& projectRoot,std::string& error)
{
    if(!audio.IsValid()) {error="Invalid Audio component";return {};}
    return ResolveAssetReference(audio.assetPath,audio.assetGuid,AssetType::Audio,registry,projectRoot,error);
}
}
