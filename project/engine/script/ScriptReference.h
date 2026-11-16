#pragma once
#include "ScriptRuntime.h"
#include "../assets/ModelReference.h"

namespace Engine::Scripting {
inline bool LoadScriptReference(ScriptRuntime& runtime, const ScriptComponent& script,
    const std::filesystem::path& projectRoot, const Assets::AssetRegistry& registry,
    std::string& error, bool reload = false)
{
    const auto key = script.RegistrationKey();
    if (script.assetGuid.empty()) {
        if (!reload && runtime.Contains(key)) { error.clear(); return true; }
        return runtime.LoadAsset(script.assetPath, projectRoot, error, reload);
    }
    const auto path = Assets::ResolveAssetReference(script.assetPath, script.assetGuid,
        Assets::AssetType::Script, registry, projectRoot, error);
    if (path.empty()) { runtime.Unregister(key); return false; }
    if (!reload && runtime.Contains(key)) { error.clear(); return true; }
    return runtime.LoadResolvedAsset(key, path, error, reload);
}
inline bool LoadScriptWorldAssets(ScriptRuntime& runtime, const Scene::EntityWorld& world,
    const std::filesystem::path& projectRoot, const Assets::AssetRegistry& registry, std::string& error)
{
    std::string firstError;size_t failures=0;
    for (const auto& entity : world.GetEntities()) {
        if (!entity.hasScript || !entity.script.enabled) continue;
        std::string current;
        if (!LoadScriptReference(runtime, entity.script, projectRoot, registry, current)) {
            if(!failures++)firstError=ScriptRuntime::DescribeLoadFailure(entity.id,entity.script.RegistrationKey(),current);
        }
    }
    error = std::move(firstError);
    if(failures>1)error+=" ("+std::to_string(failures)+" script load failures)";
    return failures==0;
}
}
