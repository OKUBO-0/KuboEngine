#pragma once
#include "ProjectPrefabRevision.h"

namespace Engine::Editor {
// Acquisition/revalidation only; no rename, source write or Scene publication.
// DELETE ownership allows a later journalled rename through THIS source handle,
// without reopening an unprotected pathname after its revision was checked.
class HeldProjectPrefabWriter final {
    friend class PrefabSourcePublication;
    friend class NativePrefabApplyEffect;
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
public:
    HeldProjectPrefabWriter()=default;
    HeldProjectPrefabWriter(const HeldProjectPrefabWriter&)=delete;
    HeldProjectPrefabWriter& operator=(const HeldProjectPrefabWriter&)=delete;
    const ProjectPrefabRevision& Snapshot() const{return snapshot_;}
    const std::filesystem::path& LockPath() const{return lockPath_;}
    void Release(){source_.reset();metadata_.reset();lock_.reset();snapshot_={};lockPath_.clear();root_.clear();}
    bool Open(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
        const ProjectPrefabRevision& expected,std::string& error) {
        try {
            const auto* record=registry.FindByGuid(expected.guid);
            if(!record || record->type!=Assets::AssetType::Prefab || !Scene::ModelComponent::IsValidGuid(expected.guid))
                throw std::runtime_error("Prefab writer requires a registered source GUID");
            const auto sourcePath=ProjectPrefabDetail::Path(project,Base::PathFromUtf8(record->absolutePath));
            if(sourcePath!=expected.file)throw std::runtime_error("Prefab writer selection changed");
            const auto assetRoot=project.ResolvePath(project.GetSettings().assetDirectory);
            const auto canonicalRoot=std::filesystem::canonical(assetRoot);
            if(std::filesystem::exists(assetRoot/".kubo/prefab-transactions"/expected.guid))throw std::runtime_error("Recover the pending Prefab source transaction first");
            const auto directory=assetRoot/".kubo/prefab-writers";
            if(!ProjectPrefabDetail::Within(std::filesystem::weakly_canonical(directory),canonicalRoot))
                throw std::runtime_error("Prefab writer directory resolves outside the asset root");
            std::filesystem::create_directories(directory);
            if(!ProjectPrefabDetail::Within(std::filesystem::canonical(directory),canonicalRoot))
                throw std::runtime_error("Prefab writer directory changed");
            const auto lockPath=directory/(expected.guid+".lock");
            auto lock=std::make_unique<NativeFile>(lockPath,GENERIC_READ|GENERIC_WRITE,OPEN_ALWAYS);
            std::string lockBytes;PrefabFileIdentity lockIdentity;std::filesystem::path lockFinal;
            HeldProjectPrefabRevision::Read(*lock,canonicalRoot,0,lockBytes,lockIdentity,lockFinal);
            if(std::filesystem::canonical(lockPath)!=lockFinal)throw std::runtime_error("Prefab writer lock pathname changed");
            ProjectPrefabRevision verified;
            {HeldProjectPrefabRevision read;if(!read.Open(project,registry,expected.guid,error,&expected))return false;verified=read.Snapshot();}
            // Read lease ended, but the cooperating writer lock remains. Acquire
            // DELETE/read ownership, then check exact identities/bytes AGAIN to
            // catch a noncooperating edit in the upgrade window.
            auto metadata=std::make_unique<NativeFile>(verified.metadata);
            auto source=std::make_unique<NativeFile>(verified.file,GENERIC_READ|DELETE);
            auto acquired=ReadOwned(*source,*metadata,canonicalRoot,verified,error);
            if(!Same(acquired,expected))throw std::runtime_error("Prefab changed while acquiring writer ownership");
            if(std::filesystem::exists(canonicalRoot/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover the pending asset move before Prefab publication");
            source_=std::move(source);metadata_=std::move(metadata);lock_=std::move(lock);
            root_=canonicalRoot;lockPath_=lockPath;snapshot_=std::move(acquired);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Revalidate(std::string& error) const {
        try {
            if(!source_ || !metadata_ || !lock_)throw std::runtime_error("Prefab writer has no acquired source");
            if(std::filesystem::exists(root_/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover the pending asset move before Prefab publication");
            const auto current=ReadOwned(*source_,*metadata_,root_,snapshot_,error);
            if(!Same(current,snapshot_))throw std::runtime_error("Owned Prefab revision changed");
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
private:
    static bool Same(const ProjectPrefabRevision& a,const ProjectPrefabRevision& b) {
        return a.file==b.file && a.metadata==b.metadata && a.guid==b.guid && a.finalFile==b.finalFile &&
            a.finalMetadata==b.finalMetadata && a.fileIdentity==b.fileIdentity && a.metadataIdentity==b.metadataIdentity &&
            a.bytes==b.bytes && a.metadataBytes==b.metadataBytes;
    }
    static ProjectPrefabRevision ReadOwned(const NativeFile& source,const NativeFile& metadata,
        const std::filesystem::path& root,const ProjectPrefabRevision& seed,std::string& error) {
        LARGE_INTEGER zero{};
        if(!SetFilePointerEx(source.handle,zero,nullptr,FILE_BEGIN) || !SetFilePointerEx(metadata.handle,zero,nullptr,FILE_BEGIN))
            throw std::runtime_error("Cannot rewind owned Prefab handles");
        auto current=seed;
        HeldProjectPrefabRevision::Read(source,root,Scene::SceneDocument::MaxBytes,current.bytes,current.fileIdentity,current.finalFile);
        HeldProjectPrefabRevision::Read(metadata,root,16384,current.metadataBytes,current.metadataIdentity,current.finalMetadata);
        if(std::filesystem::canonical(current.file)!=current.finalFile || std::filesystem::canonical(current.metadata)!=current.finalMetadata)
            throw std::runtime_error("Owned Prefab pathname changed");
        if(!ValidatePrefabDocument(HeldProjectPrefabRevision::ParseHeldJson(current.bytes),current.definition,error))throw std::runtime_error(error);
        const auto identity=HeldProjectPrefabRevision::ParseHeldJson(current.metadataBytes);
        if(!identity.is_object() || !identity.contains("version") || !identity["version"].is_number() ||
            identity["version"].get<crude_json::number>()!=1 || !identity.contains("guid") || !identity["guid"].is_string() ||
            identity["guid"].get<crude_json::string>()!=current.guid)throw std::runtime_error("Owned Prefab metadata identity changed");
        return current;
    }
    std::unique_ptr<NativeFile> source_,metadata_,lock_;
    ProjectPrefabRevision snapshot_;
    std::filesystem::path lockPath_,root_;
};
}
