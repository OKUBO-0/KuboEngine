#pragma once

#include "../scene/entity/EntityWorld.h"

#include <filesystem>
namespace Engine::Assets {class AssetRegistry;}

namespace Engine::Services {

/// @brief JSON-RPC-like command bridge for editor automation.
/// @details Transport is deliberately separate: stdio, named pipes, or an
///          actual MCP server can forward requests to this deterministic API.
class McpCommandBridge final {
public:
    explicit McpCommandBridge(std::filesystem::path projectRoot,const Assets::AssetRegistry* assets=nullptr);

    Engine::Scene::SceneValue Handle(const Engine::Scene::SceneValue& request,
        Engine::Scene::EntityWorld& world) const;

private:
    std::filesystem::path projectRoot_;
    const Assets::AssetRegistry* assets_=nullptr; // Borrowed for one Editor-thread request; never rescanned/written here.
};

} // namespace Engine::Services
