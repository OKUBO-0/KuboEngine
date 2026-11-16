#pragma once
#include "../scene/entity/EntityWorld.h"
#include "../assets/AssetRegistry.h"
namespace Engine::Editor {
void DrawParticleInspector(Scene::EntityWorld& world,Scene::EntityId id,const Assets::AssetRegistry& assets,
    const std::filesystem::path& root,std::string& status);
}
