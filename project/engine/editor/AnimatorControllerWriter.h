#pragma once
#include "../assets/AnimatorControllerAsset.h"
#include "../animation/AnimatorComponent.h"

namespace Engine::Editor {
// Acquisition only. DELETE ownership enables a later journalled handle rename;
// acquiring this object neither overwrites a source nor marks a draft saved.
class HeldAnimatorControllerWriter final {
    friend class AnimatorControllerPublication;
    friend class AnimatorControllerRecovery;
    friend class AnimatorControllerCreation;
    using Read=Assets::HeldAnimatorControllerAsset;
    using File=Read::File;
    using Revision=Assets::AnimatorControllerRevision;
    std::unique_ptr<File> source_,metadata_,lock_;
    std::unique_ptr<Revision> snapshot_;
    std::filesystem::path root_,lockPath_;
    static void ReadFile(const File& file,const std::filesystem::path& path,const std::filesystem::path& root,size_t limit,std::string& bytes,Revision::Identity& identity){
        LARGE_INTEGER zero{};if(!SetFilePointerEx(file.handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind owned Controller file");
        Read::Read(file,path,root,limit,bytes,identity);
    }
    static bool Same(const Revision& a,const Revision& b) {
        return a.source==b.source && a.metadata==b.metadata && a.guid==b.guid &&
            a.sourceIdentity==b.sourceIdentity && a.metadataIdentity==b.metadataIdentity &&
            a.bytes==b.bytes && a.metadataBytes==b.metadataBytes;
    }
    static Revision ReadOwned(const File& source,const File& metadata,const std::filesystem::path& root,const Revision& expected) {
        LARGE_INTEGER zero{};if(!SetFilePointerEx(source.handle,zero,nullptr,FILE_BEGIN) || !SetFilePointerEx(metadata.handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind Controller writer handles");
        auto current=expected;
        Read::Read(source,expected.source,root,AnimationSystem::Controller::TextCodec::MaxBytes,current.bytes,current.sourceIdentity);
        Read::Read(metadata,expected.metadata,root,16384,current.metadataBytes,current.metadataIdentity);
        if(!Same(current,expected))throw std::runtime_error("Controller source or metadata changed during writer acquisition");
        return current;
    }
public:
    const Revision* Snapshot() const noexcept{return snapshot_.get();}
    const std::filesystem::path& LockPath() const noexcept{return lockPath_;}
    void Release() noexcept{source_.reset();metadata_.reset();lock_.reset();snapshot_.reset();root_.clear();lockPath_.clear();}
    bool Open(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,const Revision& expected,std::string& error) {
        try {
            if(!project.IsOpen() || !AnimationSystem::AnimatorComponent::ValidControllerGuid(expected.guid) || expected.guid.empty())throw std::runtime_error("Controller writer needs an open project/valid GUID");
            const auto* record=registry.FindByGuid(expected.guid);
            if(!record || record->type!=Assets::AssetType::AnimatorController || Base::PathFromUtf8(record->absolutePath)!=expected.source)throw std::runtime_error("Controller writer registered selection changed");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!Read::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Controller writer asset root outside project");
            if(std::filesystem::exists(root/".kubo/controller-transactions"/expected.guid) || std::filesystem::exists(root/".kubo/controller-creations"/expected.guid) || std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover pending Controller/asset move before acquiring writer");
            const auto directory=root/".kubo/controller-writers";
            if(!Read::Within(std::filesystem::weakly_canonical(directory),root))throw std::runtime_error("Controller writer lock root escaped");
            std::filesystem::create_directories(directory);
            if(!Read::Within(std::filesystem::canonical(directory),root))throw std::runtime_error("Controller writer lock root changed");
            const auto lockPath=directory/(expected.guid+".lock");
            auto lock=std::make_unique<File>(lockPath,GENERIC_READ|GENERIC_WRITE,OPEN_ALWAYS);
            std::string lockBytes;Revision::Identity lockIdentity;
            Read::Read(*lock,lockPath,root,0,lockBytes,lockIdentity);
            Revision verified;
            {Read read;if(!read.Open(project,registry,expected.guid,error,&expected))return false;verified=*read.Snapshot();}
            // The read lease is now gone. Revalidate through DELETE/read-owned
            // handles to detect a noncooperating mutation in the upgrade gap.
            auto metadata=std::make_unique<File>(verified.metadata);
            auto source=std::make_unique<File>(verified.source,GENERIC_READ|DELETE);
            auto acquired=std::make_unique<Revision>(ReadOwned(*source,*metadata,root,verified));
            if(!Same(*acquired,expected))throw std::runtime_error("Controller writer baseline changed");
            auto ownedRoot=root,ownedLock=lockPath;
            source_.swap(source);metadata_.swap(metadata);lock_.swap(lock);snapshot_.swap(acquired);root_.swap(ownedRoot);lockPath_.swap(ownedLock);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Revalidate(std::string& error) const {
        try {
            if(!snapshot_ || !source_ || !metadata_ || !lock_)throw std::runtime_error("Controller writer ownership unavailable");
            if(std::filesystem::exists(root_/".kubo/controller-transactions"/snapshot_->guid) || std::filesystem::exists(root_/".kubo/controller-creations"/snapshot_->guid) || std::filesystem::exists(root_/".kubo/asset-move.pending.json"))throw std::runtime_error("Controller/asset transaction pending");
            ReadOwned(*source_,*metadata_,root_,*snapshot_);
            LARGE_INTEGER zero{};if(!SetFilePointerEx(lock_->handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind Controller lock");
            std::string bytes;Revision::Identity identity;Read::Read(*lock_,lockPath_,root_,0,bytes,identity);
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
