#pragma once
#include "AssetDependencies.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#pragma comment(lib, "bcrypt.lib")

namespace Engine::Assets {
namespace FingerprintDetail {
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Hash() = default;
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Open() {
        DWORD size = 0, received = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &received, 0) < 0)
            throw std::runtime_error("Cannot initialize asset SHA-256 provider");
        object.resize(size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) < 0) throw std::runtime_error("Cannot create asset SHA-256 hash");
    }
    void Add(const unsigned char* data, ULONG size) {
        if (BCryptHashData(hash, const_cast<PUCHAR>(data), size, 0) < 0) throw std::runtime_error("Cannot hash asset bytes");
    }
    void Field(const std::string& value) {
        // Length-prefix all fields, avoiding delimiter/path ambiguity.
        const auto size = std::to_string(value.size()) + ":";
        Add(reinterpret_cast<const unsigned char*>(size.data()), static_cast<ULONG>(size.size()));
        Add(reinterpret_cast<const unsigned char*>(value.data()), static_cast<ULONG>(value.size()));
    }
    std::string Finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) throw std::runtime_error("Cannot finish asset SHA-256");
        std::string result; constexpr char hex[] = "0123456789abcdef";
        for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
        return result;
    }
};
}
inline std::string HashAssetContent(const std::filesystem::path& path, const std::function<bool()>& cancelled = {}) {
    const auto check = [&] { if (cancelled && cancelled()) throw std::runtime_error("Asset content hashing cancelled"); };
    check();
    struct File { HANDLE value = INVALID_HANDLE_VALUE; ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Cannot hash ordinary asset file: " + Base::PathToUtf8(path));
    FingerprintDetail::Hash hash; hash.Open();
    std::array<unsigned char, 64 * 1024> buffer{};
    for (;;) {
        check(); DWORD count = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) throw std::runtime_error("Cannot read asset content hash");
        if (!count) break;
        hash.Add(buffer.data(), count);
    }
    check();
    return hash.Finish();
}

// Iterative transitive closure terminates on cycles. Sorted GUIDs/edge fields
// make fingerprints deterministic regardless of registry/graph traversal order.
inline std::unordered_map<std::string, std::string> BuildAssetImportFingerprints(const AssetRegistry& registry,
    const AssetDependencies& graph, const std::function<bool()>& cancelled = {}, const std::function<void()>& hashed = {}) {
    const auto check = [&] { if (cancelled && cancelled()) throw std::runtime_error("Asset fingerprint build cancelled"); };
    std::unordered_map<std::string, std::string> content, result;
    std::unordered_map<std::string, const AssetRecord*> records;
    std::unordered_map<std::string, std::vector<const AssetDependency*>> edges;
    std::unordered_map<std::string, std::vector<std::string>> issues;
    for (const auto& asset : registry.GetAssets()) {
        check(); records.emplace(asset.guid, &asset);
        content.emplace(asset.guid, HashAssetContent(Base::PathFromUtf8(asset.absolutePath), cancelled));
        if (hashed) hashed();
    }
    for (const auto& edge : graph.edges) edges[edge.ownerGuid].push_back(&edge);
    for (const auto& issue : graph.issues) issues[issue.ownerGuid].push_back(issue.message);
    for (auto& entry : issues) std::sort(entry.second.begin(), entry.second.end());
    for (auto& entry : edges) std::sort(entry.second.begin(), entry.second.end(), [](const auto* left, const auto* right) {
        return std::tie(left->targetRelative, left->targetGuid, left->reference, left->missing) <
            std::tie(right->targetRelative, right->targetGuid, right->reference, right->missing);
    });
    size_t visits = 0;
    for (const auto& asset : registry.GetAssets()) {
        check();
        std::unordered_set<std::string> visited;
        std::vector<std::string> pending{asset.guid};
        while (!pending.empty()) {
            check(); auto guid = std::move(pending.back()); pending.pop_back();
            if (!visited.insert(guid).second) continue;
            if (++visits > 1000000) throw std::runtime_error("Asset transitive fingerprint work limit exceeded");
            const auto found = edges.find(guid);
            if (found != edges.end()) for (const auto* edge : found->second) {
                if (!edge->missing && records.contains(edge->targetGuid)) pending.push_back(edge->targetGuid);
            }
        }
        std::vector<std::string> closure(visited.begin(), visited.end()); std::sort(closure.begin(), closure.end());
        FingerprintDetail::Hash digest; digest.Open(); digest.Field("kubo-import-dependency-sha256-v1");
        for (const auto& guid : closure) {
            check(); digest.Field(guid); digest.Field(records.at(guid)->relativePath); digest.Field(content.at(guid));
            const auto found = edges.find(guid);
            if (found != edges.end()) for (const auto* edge : found->second) {
                digest.Field("edge"); digest.Field(edge->targetRelative); digest.Field(edge->targetGuid);
                digest.Field(edge->reference); digest.Field(edge->missing ? "missing" : "registered");
                if(edge->kind!=AssetReferenceKind::OwnerRelative)digest.Field(edge->kind==AssetReferenceKind::Guid?"guid-reference":"project-path-reference");
            }
            const auto diagnostics = issues.find(guid);
            if (diagnostics != issues.end()) for (const auto& issue : diagnostics->second) { digest.Field("issue"); digest.Field(issue); }
            digest.Field("end-node");
        }
        result.emplace(asset.guid, digest.Finish());
    }
    check(); return result;
}
}
