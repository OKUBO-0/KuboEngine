#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace Engine::Assets {

enum class AssetType {
    Unknown,
    Scene,
    Model,
    Texture,
    Shader,
    Audio,
    Script,
    Material,
    Prefab,
    AnimatorController,
};

struct AssetRecord {
    std::string guid;
    std::string relativePath;
    std::string absolutePath;
    std::string cachePath;
    std::string fingerprint;
    std::string importFingerprint;
    AssetType type = AssetType::Unknown;
    uint64_t size = 0;
    int64_t modifiedTicks = 0;
};

/// @brief Scans project assets and persists import metadata for the editor.
class AssetRegistry final {
public:
    enum class MetadataPolicy { CreateMissing, ReadOnly };
    bool Scan(const std::filesystem::path& assetRoot, std::string& error,
        MetadataPolicy policy = MetadataPolicy::CreateMissing, const std::function<bool()>& cancelled = {});
    // Register only this newly authored source, then publish a read-only rescan.
    bool RegisterSource(const std::filesystem::path& source, std::string& guid, std::string& error);
    bool SaveDatabase(const std::filesystem::path& path, std::string& error) const;
    bool LoadDatabase(const std::filesystem::path& path, std::string& error);

    bool ImportAsset(const std::filesystem::path& source, const std::filesystem::path& cacheRoot,
        AssetRecord& result, std::string& error, const std::string& expectedImportFingerprint = {});
    bool IsImportCurrent(const AssetRecord& asset, const std::filesystem::path& cacheRoot) const;
    bool SetImportFingerprint(const std::string& guid, const std::string& fingerprint);

    const std::vector<AssetRecord>& GetAssets() const { return assets_; }
    const std::filesystem::path& GetRoot() const { return root_; }
    const AssetRecord* FindByGuid(const std::string& guid) const;
    const AssetRecord* FindByPath(const std::filesystem::path& path) const;
    std::filesystem::path ResolveGuid(const std::string& guid) const;
    static std::filesystem::path MetadataPath(const std::filesystem::path& source);
    std::vector<const AssetRecord*> Search(const std::string& query, AssetType type = AssetType::Unknown) const;

    static AssetType Classify(const std::filesystem::path& path);
    static const char* GetTypeName(AssetType type);

private:
    static std::string MakeGuid(const std::filesystem::path& relativePath, uint64_t size, int64_t modifiedTicks);
    static std::string MakeFingerprint(const std::filesystem::path& relativePath, uint64_t size, int64_t modifiedTicks);

    std::filesystem::path root_;
    std::vector<AssetRecord> assets_;
};

} // namespace Engine::Assets
