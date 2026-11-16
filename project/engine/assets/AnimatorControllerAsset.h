#pragma once
#include "AssetImportFingerprint.h"
#include "ControllerPending.h"
#include "../animation/AnimatorControllerText.h"
#include "../base/ProjectContext.h"
#include <memory>

namespace Engine::Editor {class HeldAnimatorControllerWriter;class AnimatorControllerSession;class AnimatorControllerCreationRecovery;}
namespace Engine::Assets {
struct AnimatorControllerRevision {
    struct Identity {
        DWORD volume=0,high=0,low=0;
        bool operator==(const Identity&) const = default;
    };
    std::filesystem::path source,metadata;
    Identity sourceIdentity,metadataIdentity;
    std::string guid,bytes,metadataBytes,contentHash;
    AnimationSystem::Controller::Graph graph;
};
// Windows read lease. This is NOT an atomic write/publication transaction, nor
// a hot-reload cache. A detached revision owns its bytes and decoded graph.
class HeldAnimatorControllerAsset final {
    friend class Engine::Editor::HeldAnimatorControllerWriter;
    friend class Engine::Editor::AnimatorControllerSession;
    friend class Engine::Editor::AnimatorControllerCreationRecovery;
    struct File {
        HANDLE handle=INVALID_HANDLE_VALUE;
        explicit File(const std::filesystem::path& path,DWORD access=GENERIC_READ,DWORD creation=OPEN_EXISTING) {
            handle=CreateFileW(path.c_str(),access,FILE_SHARE_READ,nullptr,creation,
                FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
            if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Controller asset missing, busy or inaccessible");
        }
        ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        File(const File&)=delete;File& operator=(const File&)=delete;
    };
    std::unique_ptr<File> source_,metadata_;
    std::unique_ptr<AnimatorControllerRevision> snapshot_;
    static bool Within(const std::filesystem::path& path,const std::filesystem::path& root) {
        auto part=path.begin();
        for(const auto& component:root) {
            if(part==path.end() || _wcsicmp(part->c_str(),component.c_str()))return false;
            ++part;
        }
        return part!=path.end();
    }
    static void Read(const File& file,const std::filesystem::path& path,const std::filesystem::path& root,
        size_t limit,std::string& bytes,AnimatorControllerRevision::Identity& identity) {
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(GetFileType(file.handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.handle,&info) ||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks!=1 ||
            (!info.nFileIndexHigh && !info.nFileIndexLow) || !GetFileSizeEx(file.handle,&size) ||
            size.QuadPart<0 || static_cast<unsigned long long>(size.QuadPart)>limit)
            throw std::runtime_error("Controller requires bounded ordinary single-link files");
        const auto required=GetFinalPathNameByHandleW(file.handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!required || required>32768)throw std::runtime_error("Cannot resolve held Controller path");
        std::wstring final(required,L'\0');
        const auto count=GetFinalPathNameByHandleW(file.handle,final.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!count || count>=required)throw std::runtime_error("Cannot resolve held Controller path");
        final.resize(count);
        if(final.starts_with(L"\\\\?\\UNC\\"))final=L"\\\\"+final.substr(8);
        else if(final.starts_with(L"\\\\?\\"))final.erase(0,4);
        const auto resolved=std::filesystem::path(final).lexically_normal();
        if(!Within(resolved,root) || std::filesystem::canonical(path)!=resolved)
            throw std::runtime_error("Controller path escaped the asset root or changed during read");
        identity={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};
        bytes.resize(static_cast<size_t>(size.QuadPart));DWORD read=0,extraRead=0;char extra=0;
        if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size() ||
            !ReadFile(file.handle,&extra,1,&extraRead,nullptr) || extraRead)
            throw std::runtime_error("Cannot read complete Controller revision");
    }
public:
    const AnimatorControllerRevision* Snapshot() const noexcept {return snapshot_.get();}
    void Release() noexcept {snapshot_.reset();source_.reset();metadata_.reset();}
    bool Open(const Base::ProjectContext& project,const AssetRegistry& registry,const std::string& guid,
        std::string& error,const AnimatorControllerRevision* expected=nullptr) {
        return OpenImpl(project,registry,guid,error,expected,false);
    }
private:
    // Only owned session acknowledgement/creation recovery may inspect an exact
    // expected revision while a record is pending. Not a normal reader option.
    bool OpenForRecovery(const Base::ProjectContext& project,const AssetRegistry& registry,const std::string& guid,const AnimatorControllerRevision& expected,std::string& error){return OpenImpl(project,registry,guid,error,&expected,true);}
    bool OpenImpl(const Base::ProjectContext& project,const AssetRegistry& registry,const std::string& guid,
        std::string& error,const AnimatorControllerRevision* expected,bool recovery) {
        try {
            using AnimationSystem::Controller::TextCodec;
            if(!project.IsOpen())throw std::runtime_error("Controller requires an open project");
            const auto* record=registry.FindByGuid(guid);
            if(!record || record->type!=AssetType::AnimatorController)throw std::runtime_error("Select a registered Controller GUID");
            const auto assetRoot=project.ResolvePath(project.GetSettings().assetDirectory);
            const auto root=std::filesystem::canonical(assetRoot);
            if(!Within(root,std::filesystem::canonical(project.GetRootPath())) ||
                std::filesystem::exists(assetRoot/".kubo/asset-move.pending.json"))
                throw std::runtime_error("Controller asset root invalid or asset move pending");
            if(!recovery)AssertControllerResolved(root,guid);
            auto candidate=std::make_unique<AnimatorControllerRevision>();candidate->guid=guid;
            candidate->source=Base::PathFromUtf8(record->absolutePath).lexically_normal();
            candidate->metadata=AssetRegistry::MetadataPath(candidate->source);
            if(!candidate->source.is_absolute() || !Within(candidate->source,assetRoot.lexically_normal()) ||
                AssetRegistry::Classify(candidate->source)!=AssetType::AnimatorController)
                throw std::runtime_error("Controller is outside the active asset root");
            auto source=std::make_unique<File>(candidate->source),metadata=std::make_unique<File>(candidate->metadata);
            Read(*source,candidate->source,root,TextCodec::MaxBytes,candidate->bytes,candidate->sourceIdentity);
            Read(*metadata,candidate->metadata,root,16384,candidate->metadataBytes,candidate->metadataIdentity);
            if(!TextCodec::ValidateJson(candidate->metadataBytes,error))return false;
            const auto identity=crude_json::value::parse(candidate->metadataBytes);
            if(!identity.is_object() || !identity.contains("version") || !identity["version"].is_number() ||
                identity["version"].get<double>()!=1 || !identity.contains("guid") || !identity["guid"].is_string() ||
                identity["guid"].get<crude_json::string>()!=guid)
                throw std::runtime_error("Controller metadata identity mismatch");
            AssetRegistry fresh;
            if(!fresh.Scan(assetRoot,error,AssetRegistry::MetadataPolicy::ReadOnly))return false;
            const auto* current=fresh.FindByGuid(guid);
            if(!current || current->type!=AssetType::AnimatorController ||
                Base::PathFromUtf8(current->absolutePath)!=candidate->source)
                throw std::runtime_error("Controller selection stale; refresh assets");
            if(!TextCodec::Decode(candidate->bytes,candidate->graph,error))return false;
            // Reopen the pathnames while the original handles remain held: an
            // ancestor junction may be replaced despite a leaf read lease.
            // This is a final observation, not a lock on the directory tree.
            File verifySource(candidate->source),verifyMetadata(candidate->metadata);
            std::string verifiedBytes,verifiedMetadata;
            AnimatorControllerRevision::Identity verifiedSourceIdentity,verifiedMetadataIdentity;
            Read(verifySource,candidate->source,root,TextCodec::MaxBytes,verifiedBytes,verifiedSourceIdentity);
            Read(verifyMetadata,candidate->metadata,root,16384,verifiedMetadata,verifiedMetadataIdentity);
            if(std::filesystem::canonical(assetRoot)!=root || verifiedSourceIdentity!=candidate->sourceIdentity ||
                verifiedMetadataIdentity!=candidate->metadataIdentity || verifiedBytes!=candidate->bytes ||
                verifiedMetadata!=candidate->metadataBytes)
                throw std::runtime_error("Controller pathname changed during validation");
            FingerprintDetail::Hash hash;hash.Open();
            hash.Add(reinterpret_cast<const unsigned char*>(candidate->bytes.data()),static_cast<ULONG>(candidate->bytes.size()));
            candidate->contentHash=hash.Finish();
            if(expected && (expected->guid!=candidate->guid || expected->source!=candidate->source ||
                expected->metadata!=candidate->metadata || expected->sourceIdentity!=candidate->sourceIdentity ||
                expected->metadataIdentity!=candidate->metadataIdentity || expected->bytes!=candidate->bytes ||
                expected->metadataBytes!=candidate->metadataBytes))
                throw std::runtime_error("Controller source or identity changed; reload the revision");
            if(!recovery)AssertControllerResolved(root,guid);
            source_=std::move(source);metadata_=std::move(metadata);snapshot_=std::move(candidate);
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
