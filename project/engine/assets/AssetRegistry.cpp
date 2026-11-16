#include "AssetRegistry.h"
#include "engine/animation/AnimatorControllerText.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include "externals/imgui_node_editor/crude_json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Engine::Assets {
namespace {

constexpr size_t kMaxDatabaseBytes = 16 * 1024 * 1024;

std::string Utf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path FromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
bool ValidGuid(const std::string& guid)
{
    return (guid.size() == 16 || guid.size() == 32) && std::all_of(guid.begin(), guid.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string RandomGuid()
{
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot create asset identity");
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
bool Within(const std::filesystem::path& path, const std::filesystem::path& root)
{
    auto part = path.begin();
    for (const auto& component : root) {
        if (part == path.end() || CompareStringOrdinal(component.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
        ++part;
    }
    return part != path.end();
}
crude_json::value ReadJson(const std::filesystem::path& path, size_t limit)
{
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("Asset JSON exceeds its size limit");
    std::ifstream input(path, std::ios::binary);
    std::string text(static_cast<size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(size)) || input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Cannot read asset JSON or it changed while reading");
    if(text.find('\0')!=std::string::npos || (!text.empty() &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0))
        throw std::runtime_error("Invalid asset JSON UTF-8");
    size_t depth=0,structures=0;bool quoted=false,escaped=false;
    for(const char c:text) {
        if(quoted) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='"') quoted=false;}
        else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') {if(++depth>128 || ++structures>65536) throw std::runtime_error("Asset JSON structure limit exceeded");}
        else if(c=='}' || c==']') {if(!depth) throw std::runtime_error("Invalid asset JSON structure");--depth;}
    }
    return crude_json::value::parse(text);
}
std::string ReadIdentity(const std::filesystem::path& path)
{
    const auto value = ReadJson(path, 16 * 1024);
    if (!value.is_object() || !value.contains("version") || !value["version"].is_number() ||
        value["version"].get<double>() != 1 || !value.contains("guid") || !value["guid"].is_string() ||
        !ValidGuid(value["guid"].get<crude_json::string>())) throw std::runtime_error("Invalid asset identity metadata: " + Utf8(path));
    return value["guid"].get<crude_json::string>();
}
void WriteHandle(HANDLE handle, const std::string& text)
{
    DWORD written = 0;
    if (!WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(handle)) throw std::runtime_error("Cannot write asset metadata");
}
std::string EnsureIdentity(const std::filesystem::path& source, const std::string& fallback,
    AssetRegistry::MetadataPolicy policy)
{
    const auto path = AssetRegistry::MetadataPath(source);
    if (std::filesystem::is_symlink(path)) throw std::runtime_error("Linked asset identity metadata is not supported");
    if (std::filesystem::exists(path)) return ReadIdentity(path);
    if (policy == AssetRegistry::MetadataPolicy::ReadOnly) return fallback;
    // Preserve an existing database identity when migrating; otherwise use 128 random bits.
    const auto guid = fallback.empty() ? RandomGuid() : fallback;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) return ReadIdentity(path);
        throw std::runtime_error("Cannot create asset identity metadata: " + Utf8(path));
    }
    try {
        crude_json::value value;
        value["version"] = 1.0;
        value["guid"] = guid;
        WriteHandle(file, value.dump(2));
        CloseHandle(file);
        return guid;
    } catch (...) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(file);
        throw;
    }
}
void WriteJson(const std::filesystem::path& path, const crude_json::value& value)
{
    const auto text = value.dump(2);
    if (text.size() > kMaxDatabaseBytes) throw std::runtime_error("Asset database exceeds the 16 MiB limit");
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += L".kubo-" + std::filesystem::path(RandomGuid()).wstring() + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create asset database temporary file");
    try { WriteHandle(file, text); }
    catch (...) { CloseHandle(file); DeleteFileW(temporary.c_str()); throw; }
    CloseHandle(file);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot replace asset metadata/database");
    }
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

int64_t GetModifiedTicks(const std::filesystem::path& path)
{
    const auto time = std::filesystem::last_write_time(path);
    return time.time_since_epoch().count();
}

crude_json::value ToJson(const AssetRecord& record)
{
    crude_json::value value;
    value["guid"] = record.guid;
    value["relativePath"] = record.relativePath;
    value["absolutePath"] = record.absolutePath;
    value["cachePath"] = record.cachePath;
    value["fingerprint"] = record.fingerprint;
    value["importFingerprint"] = record.importFingerprint;
    value["type"] = AssetRegistry::GetTypeName(record.type);
    value["size"] = static_cast<double>(record.size);
    value["modifiedTicks"] = static_cast<double>(record.modifiedTicks);
    return value;
}

AssetRecord FromJson(const crude_json::value& value)
{
    if (!value.is_object() || !value.contains("guid") || !value.contains("relativePath") ||
        !value.contains("absolutePath") || !value.contains("cachePath") || !value.contains("type") ||
        !value.contains("size") || !value.contains("modifiedTicks") || !value["guid"].is_string() ||
        !value["relativePath"].is_string() || !value["absolutePath"].is_string() ||
        !value["cachePath"].is_string() || !value["type"].is_string() ||
        !value["size"].is_number() || !value["modifiedTicks"].is_number()) {
        throw std::runtime_error("Invalid asset record");
    }
    AssetRecord record;
    record.guid = value["guid"].get<crude_json::string>();
    if (!ValidGuid(record.guid)) throw std::runtime_error("Invalid asset database GUID");
    record.relativePath = value["relativePath"].get<crude_json::string>();
    record.absolutePath = value["absolutePath"].get<crude_json::string>();
    record.cachePath = value["cachePath"].get<crude_json::string>();
    if (value.contains("fingerprint") && value["fingerprint"].is_string()) {
        record.fingerprint = value["fingerprint"].get<crude_json::string>();
    }
    const auto type = Lower(value["type"].get<crude_json::string>());
    if (value.contains("importFingerprint")) {
        if (!value["importFingerprint"].is_string()) throw std::runtime_error("Invalid import fingerprint field");
        record.importFingerprint = value["importFingerprint"].get<crude_json::string>();
        if (!record.importFingerprint.empty() && (record.importFingerprint.size() != 64 ||
            !std::all_of(record.importFingerprint.begin(), record.importFingerprint.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))) throw std::runtime_error("Invalid import fingerprint digest");
    }
    for (int index = static_cast<int>(AssetType::Unknown); index <= static_cast<int>(AssetType::AnimatorController); ++index) {
        const auto candidate = static_cast<AssetType>(index);
        if (Lower(AssetRegistry::GetTypeName(candidate)) == type) {
            record.type = candidate;
            break;
        }
    }
    const auto size = value["size"].get<crude_json::number>();
    const auto ticks = value["modifiedTicks"].get<crude_json::number>();
    if (!std::isfinite(size) || size < 0 || size >= double(UINT64_MAX) || std::floor(size) != size ||
        !std::isfinite(ticks) || ticks <= double(INT64_MIN) || ticks >= double(INT64_MAX) || std::floor(ticks) != ticks)
        throw std::runtime_error("Invalid asset database numeric fields");
    record.size = static_cast<uint64_t>(size);
    record.modifiedTicks = static_cast<int64_t>(ticks);
    return record;
}

} // namespace

bool AssetRegistry::Scan(const std::filesystem::path& assetRoot, std::string& error, MetadataPolicy policy, const std::function<bool()>& cancelled)
{
    try {
        if (cancelled && cancelled()) throw std::runtime_error("Asset scan cancelled");
        if (std::filesystem::exists(assetRoot / ".kubo/asset-move.pending.json"))
            throw std::runtime_error("Pending asset move requires recovery: " + Utf8(assetRoot / ".kubo/asset-move.pending.json"));
        if (!std::filesystem::is_directory(assetRoot)) {
            throw std::runtime_error("Asset root is not a directory");
        }
        std::vector<AssetRecord> scanned;
        const auto absoluteRoot = std::filesystem::canonical(assetRoot);
        std::unordered_set<std::string> identities;
        std::error_code iteratorError;
        for (auto iterator = std::filesystem::recursive_directory_iterator(absoluteRoot, iteratorError);
            iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(iteratorError)) {
            if (cancelled && cancelled()) throw std::runtime_error("Asset scan cancelled");
            if (iteratorError) {
                throw std::runtime_error("Asset scan failed: " + iteratorError.message());
            }
            const auto& entry = *iterator;
            const auto relative = entry.path().lexically_relative(absoluteRoot);
            const auto first = relative.begin();
            if (first != relative.end() && (*first == L".kubo" || *first == L".git" || *first == L".vs" ||
                *first == L"Library" || *first == L"Build" || *first == L"Builds" || *first == L"generated")) {
                if (entry.is_directory()) iterator.disable_recursion_pending();
                continue;
            }
            if (Lower(Utf8(entry.path().filename())).ends_with(".kubo-meta.json")) continue;
            if (entry.is_symlink()) throw std::runtime_error("Asset symlinks are not supported");
            if (!Within(std::filesystem::weakly_canonical(entry.path()), absoluteRoot))
                throw std::runtime_error("Asset resolves outside its root: " + Utf8(entry.path()));
            const bool regular = entry.is_regular_file(iteratorError);
            if (iteratorError) throw std::runtime_error("Cannot inspect asset: " + Utf8(entry.path()));
            if (!regular) continue;
            AssetRecord record;
            record.relativePath = Utf8(relative);
            record.absolutePath = Utf8(entry.path().lexically_normal());
            record.type = Classify(entry.path());
            record.size = entry.file_size(iteratorError);
            if (iteratorError) {
                throw std::runtime_error("Cannot read asset size: " + Utf8(entry.path()));
            }
            record.modifiedTicks = GetModifiedTicks(entry.path());
            // Read all existing identities before creating any. Otherwise a
            // replacement at an old path could steal a moved asset's cached GUID.
            record.guid = EnsureIdentity(entry.path(), {}, MetadataPolicy::ReadOnly);
            if (!record.guid.empty() && !identities.insert(record.guid).second)
                throw std::runtime_error("Duplicate asset GUID: " + record.guid);
            record.fingerprint = MakeFingerprint(relative, record.size, record.modifiedTicks);
            scanned.push_back(std::move(record));
        }
        if (iteratorError) throw std::runtime_error("Asset scan failed: " + iteratorError.message());
        for (auto& record : scanned) {
            if (cancelled && cancelled()) throw std::runtime_error("Asset scan cancelled");
            if (record.guid.empty()) {
                const auto old = std::find_if(assets_.begin(), assets_.end(),
                    [&record](const AssetRecord& candidate) { return candidate.relativePath == record.relativePath; });
                // Only legacy path-hash identities migrate from a database.
                // Missing modern sidecars are never reconstructed by path.
                const auto legacy = old != assets_.end() && root_ == absoluteRoot && old->guid.size() == 16 &&
                    !identities.contains(old->guid) ? old->guid : std::string{};
                const auto relative = FromUtf8(record.relativePath);
                record.guid = EnsureIdentity(FromUtf8(record.absolutePath), policy == MetadataPolicy::ReadOnly && legacy.empty()
                    ? MakeGuid(relative, 0, 0) : legacy, policy);
                if (!identities.insert(record.guid).second) throw std::runtime_error("Duplicate asset GUID: " + record.guid);
            }
            if (const auto previous = FindByGuid(record.guid); previous && root_ == absoluteRoot) record.cachePath = previous->cachePath;
        }
        std::sort(scanned.begin(), scanned.end(), [](const AssetRecord& left, const AssetRecord& right) {
            return left.relativePath < right.relativePath;
        });
        if (cancelled && cancelled()) throw std::runtime_error("Asset scan cancelled");
        root_ = absoluteRoot;
        assets_ = std::move(scanned);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool AssetRegistry::SaveDatabase(const std::filesystem::path& path, std::string& error) const
{
    try {
        crude_json::value root;
        root["version"] = 3.0;
        root["assetRoot"] = Utf8(root_);
        root["assets"] = crude_json::array{};
        for (const auto& asset : assets_) {
            root["assets"].push_back(ToJson(asset));
        }
        WriteJson(path, root);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool AssetRegistry::LoadDatabase(const std::filesystem::path& path, std::string& error)
{
    try {
        const auto root = ReadJson(path, kMaxDatabaseBytes);
        if (!root.is_object() || !root.contains("version") || !root["version"].is_number() ||
            (root["version"].get<crude_json::number>() != 1.0 && root["version"].get<crude_json::number>() != 2.0 && root["version"].get<crude_json::number>() != 3.0) || !root.contains("assetRoot") ||
            !root["assetRoot"].is_string() || !root.contains("assets") || !root["assets"].is_array()) {
            throw std::runtime_error("Invalid asset database");
        }
        std::vector<AssetRecord> loaded;
        std::unordered_set<std::string> identities;
        std::unordered_set<std::string> paths;
        for (const auto& value : root["assets"].get<crude_json::array>()) {
            auto record = FromJson(value);
            if (!identities.insert(record.guid).second) throw std::runtime_error("Duplicate asset database GUID");
            if (!paths.insert(Lower(record.relativePath)).second) throw std::runtime_error("Duplicate asset database path");
            loaded.push_back(std::move(record));
        }
        root_ = FromUtf8(root["assetRoot"].get<crude_json::string>());
        assets_ = std::move(loaded);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool AssetRegistry::ImportAsset(const std::filesystem::path& source, const std::filesystem::path& cacheRoot,
    AssetRecord& result, std::string& error, const std::string& expectedImportFingerprint)
{
    try {
        if (!std::filesystem::is_regular_file(source)) {
            throw std::runtime_error("Asset source is not a regular file");
        }
        const auto absoluteSource = std::filesystem::canonical(source);
        const auto assetRoot = root_.empty() ? absoluteSource.parent_path() : root_;
        if (!Within(absoluteSource, assetRoot) || Lower(Utf8(absoluteSource.filename())).ends_with(".kubo-meta.json"))
            throw std::runtime_error("Import source is outside the asset root or is identity metadata");
        AssetRecord imported;
        const auto relative = absoluteSource.lexically_relative(assetRoot);
        imported.absolutePath = Utf8(absoluteSource);
        imported.relativePath = Utf8(relative);
        imported.type = Classify(source);
        if (imported.type == AssetType::AnimatorController) {
            using namespace Engine::AnimationSystem::Controller;
            const auto bytes = std::filesystem::file_size(absoluteSource);
            if (bytes > TextCodec::MaxBytes) throw std::runtime_error("Controller source exceeds byte bound");
            std::ifstream input(absoluteSource, std::ios::binary);
            std::string text(static_cast<size_t>(bytes), '\0');
            if (!input.read(text.data(), static_cast<std::streamsize>(bytes)) || input.peek() != EOF)
                throw std::runtime_error("Cannot read stable controller text");
            Graph graph;std::string controllerError;
            if (!TextCodec::Decode(text, graph, controllerError)) throw std::runtime_error(controllerError);
        }
        imported.size = std::filesystem::file_size(source);
        imported.modifiedTicks = GetModifiedTicks(source);
        // Scan performs legacy migration. Import must never infer an identity
        // from a stale path cache when the source has no sidecar.
        imported.guid = EnsureIdentity(absoluteSource, {}, MetadataPolicy::CreateMissing);
        if (const auto other = FindByGuid(imported.guid); other && other->relativePath != imported.relativePath)
            throw std::runtime_error("Duplicate imported asset GUID");
        imported.fingerprint = MakeFingerprint(relative, imported.size, imported.modifiedTicks);
        if (!expectedImportFingerprint.empty()) {
            const auto* previous = FindByGuid(imported.guid);
            if (!previous || previous->fingerprint != imported.fingerprint || previous->importFingerprint != expectedImportFingerprint)
                throw std::runtime_error("Asset changed after dependency hashing; refresh again");
            imported.importFingerprint = expectedImportFingerprint;
        }
        const auto cacheDirectory = std::filesystem::absolute(cacheRoot);
        std::filesystem::create_directories(cacheDirectory);
        const auto metadataPath = cacheDirectory / (imported.guid + ".meta.json");
        imported.cachePath = Utf8(metadataPath.lexically_normal());
        WriteJson(metadataPath, ToJson(imported));
        const auto existing = std::find_if(assets_.begin(), assets_.end(),
            [&imported](const AssetRecord& asset) { return asset.relativePath == imported.relativePath; });
        if (existing == assets_.end()) { assets_.push_back(imported); }
        else { *existing = imported; }
        root_ = assetRoot;
        result = std::move(imported);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool AssetRegistry::IsImportCurrent(const AssetRecord& asset, const std::filesystem::path& cacheRoot) const
{
    try {
        if (!ValidGuid(asset.guid) || asset.cachePath.empty()) return false;
        const auto expected = std::filesystem::absolute(cacheRoot) / (asset.guid + ".meta.json");
        if (FromUtf8(asset.cachePath).lexically_normal() != expected.lexically_normal() ||
            std::filesystem::is_symlink(expected)) return false;
        return ReadJson(expected, 16 * 1024).dump() == ToJson(asset).dump();
    } catch (...) { return false; }
}

bool AssetRegistry::SetImportFingerprint(const std::string& guid, const std::string& fingerprint)
{
    if (fingerprint.size() != 64 || !std::all_of(fingerprint.begin(), fingerprint.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return false;
    for (auto& asset : assets_) if (asset.guid == guid) { asset.importFingerprint = fingerprint; return true; }
    return false;
}

const AssetRecord* AssetRegistry::FindByGuid(const std::string& guid) const
{
    const auto found = std::find_if(assets_.begin(), assets_.end(),
        [&guid](const AssetRecord& asset) { return asset.guid == guid; });
    return found == assets_.end() ? nullptr : &*found;
}

const AssetRecord* AssetRegistry::FindByPath(const std::filesystem::path& path) const
{
    std::error_code error;
    auto absolute = std::filesystem::weakly_canonical(path, error);
    if (error) return nullptr;
    absolute.make_preferred();
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const AssetRecord& asset) {
        auto candidate = FromUtf8(asset.absolutePath);
        candidate.make_preferred();
        return CompareStringOrdinal(candidate.c_str(), -1, absolute.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    return found == assets_.end() ? nullptr : &*found;
}

std::filesystem::path AssetRegistry::ResolveGuid(const std::string& guid) const
{
    const auto* asset = FindByGuid(guid);
    return asset ? FromUtf8(asset->absolutePath) : std::filesystem::path{};
}

std::filesystem::path AssetRegistry::MetadataPath(const std::filesystem::path& source)
{
    auto path = source;
    path += L".kubo-meta.json";
    return path;
}

std::vector<const AssetRecord*> AssetRegistry::Search(const std::string& query, AssetType type) const
{
    const auto needle = Lower(query);
    std::vector<const AssetRecord*> result;
    for (const auto& asset : assets_) {
        if (type != AssetType::Unknown && asset.type != type) { continue; }
        if (needle.empty() || Lower(asset.relativePath).find(needle) != std::string::npos) {
            result.push_back(&asset);
        }
    }
    return result;
}

AssetType AssetRegistry::Classify(const std::filesystem::path& path)
{
    const auto extension = Lower(Utf8(path.extension()));
    if (extension == ".json" || extension == ".scene") { return AssetType::Scene; }
    if (extension == ".prefab") { return AssetType::Prefab; }
    if (extension == ".animfsm") { return AssetType::AnimatorController; }
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") { return AssetType::Model; }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".dds" || extension == ".tga") { return AssetType::Texture; }
    if (extension == ".hlsl" || extension == ".hlsli" || extension == ".cso") { return AssetType::Shader; }
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg") { return AssetType::Audio; }
    if (extension == ".kubo" || extension == ".lua" || extension == ".cs" || extension == ".cpp" || extension == ".h") { return AssetType::Script; }
    if (extension == ".mat" || extension == ".material") { return AssetType::Material; }
    return AssetType::Unknown;
}

const char* AssetRegistry::GetTypeName(AssetType type)
{
    switch (type) {
    case AssetType::Scene: return "Scene";
    case AssetType::Model: return "Model";
    case AssetType::Texture: return "Texture";
    case AssetType::Shader: return "Shader";
    case AssetType::Audio: return "Audio";
    case AssetType::Script: return "Script";
    case AssetType::Material: return "Material";
    case AssetType::Prefab: return "Prefab";
    case AssetType::AnimatorController: return "AnimatorController";
    default: return "Unknown";
    }
}

std::string AssetRegistry::MakeGuid(const std::filesystem::path& relativePath, uint64_t size, int64_t modifiedTicks)
{
    std::string key = Utf8(relativePath) + "|" + std::to_string(size) + "|" + std::to_string(modifiedTicks);
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char character : key) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string AssetRegistry::MakeFingerprint(const std::filesystem::path& relativePath, uint64_t size, int64_t modifiedTicks)
{
    return MakeGuid(relativePath, size, modifiedTicks);
}

} // namespace Engine::Assets
