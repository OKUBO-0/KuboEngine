#pragma once

#include <filesystem>
#include <string>

namespace Engine::Services {

struct UpdateInfo {
    bool valid = false;
    bool updateAvailable = false;
    std::string version;
    std::string url;
    std::string sha256;
    std::string artifact;
    std::string error;
};

/// @brief Verifies and compares a local/previously downloaded update manifest.
/// @details Transport and installation stay outside the engine so an update
///          cannot replace files without explicit user approval.
class UpdateService final {
public:
    static UpdateInfo ReadManifest(const std::filesystem::path& manifestPath, const std::string& currentVersion);
    static int CompareVersions(const std::string& left, const std::string& right);
    static bool ComputeSha256(const std::filesystem::path& file, std::string& hash, std::string& error);
    static bool StageVerifiedArtifact(const std::filesystem::path& source,
        const std::filesystem::path& destination, const std::string& expectedSha256, std::string& error);
};

} // namespace Engine::Services
