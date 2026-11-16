#pragma once
#include "AssetRegistry.h"
#include "../scene/entity/EntityWorld.h"
#include <fstream>
#include <stdexcept>

namespace Engine::Assets {
// Fail closed for GUID references: never substitute an unrelated old-path asset.
inline std::filesystem::path ResolveAssetReference(const std::string& assetPath, const std::string& assetGuid,
    AssetType expectedType, const AssetRegistry& registry, const std::filesystem::path& projectRoot, std::string& error)
{
    error.clear();
    try {
    std::filesystem::path path;
    if (!assetGuid.empty()) {
        if (!registry.GetRoot().empty() && std::filesystem::exists(registry.GetRoot() / ".kubo/asset-move.pending.json")) {
            error = "Pending asset move requires recovery before GUID resolution"; return {};
        }
        if (!Scene::ModelComponent::IsValidGuid(assetGuid)) { error = "Invalid asset GUID"; return {}; }
        const auto* record = registry.FindByGuid(assetGuid);
        if (!record || record->type != expectedType) { error = "Asset GUID missing or incorrect type: " + assetGuid; return {}; }
        path = registry.ResolveGuid(assetGuid);
        // A stale registry must not resolve a replacement at the old filename.
        try {
            const auto metadata = AssetRegistry::MetadataPath(path);
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
                std::filesystem::is_symlink(std::filesystem::symlink_status(metadata)))
                throw std::runtime_error("Linked asset identity/source is not supported");
            const auto size = std::filesystem::file_size(metadata);
            if (size > 16 * 1024) throw std::runtime_error("Oversized asset identity metadata");
            std::ifstream input(metadata, std::ios::binary);
            std::string bytes(static_cast<size_t>(size), '\0');
            if (!input.read(bytes.data(), static_cast<std::streamsize>(size)) || input.peek() != EOF)
                throw std::runtime_error("Cannot read asset identity metadata");
            const auto json = Scene::SceneValue::parse(bytes);
            if (!json.is_object() || !json.contains("version") || !json["version"].is_number() ||
                json["version"].get<crude_json::number>() != 1 || !json.contains("guid") ||
                !json["guid"].is_string() || json["guid"].get<crude_json::string>() != assetGuid)
                throw std::runtime_error("Asset identity metadata does not match GUID");
        } catch (const std::exception& exception) { error = exception.what(); return {}; }
    } else {
        path = std::filesystem::path(std::u8string(assetPath.begin(), assetPath.end()));
        if (path.empty()) { error = "Empty asset reference"; return {}; }
        if (path.is_relative()) path = projectRoot / path;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) { error = "Asset source is missing"; return {}; }
    return path;
    } catch (const std::exception& exception) { error = exception.what(); return {}; }
}
inline std::filesystem::path ResolveModelReference(const Scene::ModelComponent& model,
    const AssetRegistry& registry, const std::filesystem::path& projectRoot, std::string& error)
{
    return ResolveAssetReference(model.assetPath, model.assetGuid, AssetType::Model, registry, projectRoot, error);
}
}
