#pragma once
#include "AnimatorControllerPublication.h"
#include "../assets/AssetMove.h"
#include <array>

namespace Engine::Editor {
// New asset pair, not an overwrite. Intent and stages are retained until a
// separate creation recovery/adoption acknowledgement completes.
class AnimatorControllerCreation final {
    friend class AnimatorControllerSession;
    using Writer=HeldAnimatorControllerWriter;
    using File=Writer::File;
    using Revision=Assets::AnimatorControllerRevision;
    using Publication=AnimatorControllerPublication;
    std::unique_ptr<File> lock_,source_,metadata_,journal_;
    std::shared_ptr<const Revision> next_;
    std::filesystem::path root_,relative_,directory_,sourceStage_,metadataStage_,journalPath_;
    std::string journalBytes_;Revision::Identity journalIdentity_;
    bool used_=false,prepared_=false,attempted_=false,publishing_=false,metadataPublished_=false,published_=false;
    static void Absent(const std::filesystem::path& path){
        const auto attributes=GetFileAttributesW(path.c_str());const auto code=GetLastError();
        if(attributes!=INVALID_FILE_ATTRIBUTES || (code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND))throw std::runtime_error("Controller creation destination occupied/inaccessible");
    }
    static std::string Guid(){
        std::array<unsigned char,16> bytes{};
        if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create Controller GUID");
        std::string value;value.reserve(32);for(const auto byte:bytes){value+="0123456789abcdef"[byte>>4];value+="0123456789abcdef"[byte&15];}return value;
    }
    void CheckRoot() const {
        if(std::filesystem::canonical(root_)!=root_ || std::filesystem::exists(root_/".kubo/asset-move.pending.json"))throw std::runtime_error("Controller creation root changed/asset move pending");
        Assets::MoveDetail::CheckParents(root_,relative_);
        if(!Publication::Within(std::filesystem::weakly_canonical(root_/relative_),root_))throw std::runtime_error("Controller creation path escaped");
    }
    void Verify(const File& file,const std::filesystem::path& path,const std::string& expected,const Revision::Identity& identity,size_t limit) const {
        std::string bytes;Revision::Identity id;Writer::ReadFile(file,path,root_,limit,bytes,id);
        if(bytes!=expected || id!=identity)throw std::runtime_error("Controller creation held revision changed");
    }
    void Revalidate() const {
        CheckRoot();Absent(next_->source);Absent(next_->metadata);
        std::string empty;Revision::Identity id;Writer::ReadFile(*lock_,root_/".kubo/controller-create.lock",root_,0,empty,id);
        Verify(*source_,sourceStage_,next_->bytes,next_->sourceIdentity,AnimationSystem::Controller::TextCodec::MaxBytes);
        Verify(*metadata_,metadataStage_,next_->metadataBytes,next_->metadataIdentity,16384);
        Verify(*journal_,journalPath_,journalBytes_,journalIdentity_,2*1024*1024);
    }
public:
    bool IsPublished() const noexcept{return published_;}
    bool HasPublishedMetadata() const noexcept{return metadataPublished_;}
    const std::filesystem::path& Directory() const noexcept{return directory_;}
    const Revision* PublishedRevision() const noexcept{return published_?next_.get():nullptr;}
    bool Prepare(const Base::ProjectContext& project,const std::string& assetRelative,std::string_view text,std::string& error){
        try{
            if(used_)throw std::runtime_error("Controller creation already used");used_=true;
            if(!project.IsOpen() || !Scripting::ValidScriptText(assetRelative,4096))throw std::runtime_error("Controller creation requires project/UTF-8 relative path");
            auto next=std::make_shared<Revision>();if(!AnimationSystem::Controller::TextCodec::Decode(text,next->graph,error))return false;next->bytes=std::string(text);
            auto relative=Assets::MoveDetail::Relative(assetRelative);
            if(Assets::AssetRegistry::Classify(relative)!=Assets::AssetType::AnimatorController)throw std::runtime_error("New Controller requires .animfsm extension");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!Publication::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Controller creation asset root outside project");
            root_=root;relative_=relative;CheckRoot();next->source=root/relative;next->metadata=Assets::AssetRegistry::MetadataPath(next->source);Absent(next->source);Absent(next->metadata);
            const auto storage=root/".kubo";
            if(!Publication::Within(std::filesystem::weakly_canonical(storage),root))throw std::runtime_error("Controller creation storage escaped");
            std::filesystem::create_directories(storage);const auto storageAttributes=GetFileAttributesW(storage.c_str());
            if(storageAttributes==INVALID_FILE_ATTRIBUTES || (storageAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Controller creation storage linked");
            lock_=std::make_unique<File>(storage/"controller-create.lock",GENERIC_READ|GENERIC_WRITE,OPEN_ALWAYS);
            std::string empty;Revision::Identity id;Writer::ReadFile(*lock_,storage/"controller-create.lock",root,0,empty,id);
            Assets::AssetRegistry fresh;if(!fresh.Scan(root,error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))return false;
            next->guid=Guid();if(fresh.FindByGuid(next->guid))throw std::runtime_error("New Controller GUID collision");
            const auto parent=storage/"controller-creations";
            if(!Publication::Within(std::filesystem::weakly_canonical(parent),root))throw std::runtime_error("Controller creation transaction escaped");
            std::filesystem::create_directories(parent);const auto attributes=GetFileAttributesW(parent.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Publication::Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Controller creation transaction linked/changed");
            const auto directory=parent/next->guid;if(!std::filesystem::create_directory(directory))throw std::runtime_error("Controller creation transaction occupied");
            directory_=directory;sourceStage_=directory/"source.stage";metadataStage_=directory/"metadata.stage";journalPath_=directory/"pending.json";
            crude_json::value meta;meta["version"]=1.0;meta["guid"]=next->guid;next->metadataBytes=meta.dump();
            {File source(sourceStage_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);Writer::ReadFile(source,sourceStage_,root,0,empty,next->sourceIdentity);Publication::Write(source.handle,next->bytes);}
            {File metadata(metadataStage_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);Writer::ReadFile(metadata,metadataStage_,root,0,empty,next->metadataIdentity);Publication::Write(metadata.handle,next->metadataBytes);}
            source_=std::make_unique<File>(sourceStage_,GENERIC_READ|DELETE);metadata_=std::make_unique<File>(metadataStage_,GENERIC_READ|DELETE);
            Assets::FingerprintDetail::Hash hash;hash.Open();hash.Add(reinterpret_cast<const unsigned char*>(next->bytes.data()),static_cast<ULONG>(next->bytes.size()));next->contentHash=hash.Finish();
            crude_json::value intent;intent["format"]="kubo-controller-creation-1";intent["guid"]=next->guid;intent["sourceRelative"]=assetRelative;
            intent["sourceBytes"]=next->bytes;intent["metadataBytes"]=next->metadataBytes;intent["sourceIdentity"]=Publication::Identity(next->sourceIdentity);intent["metadataIdentity"]=Publication::Identity(next->metadataIdentity);
            journalBytes_=intent.dump();if(journalBytes_.size()>2*1024*1024)throw std::runtime_error("Controller creation intent too large");
            journal_=std::make_unique<File>(journalPath_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);Writer::ReadFile(*journal_,journalPath_,root,0,empty,journalIdentity_);Publication::Write(journal_->handle,journalBytes_);
            next_=std::move(next);Revalidate();prepared_=true;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Publish(std::string& error,const std::function<void()>& afterMetadata={}){
        if(publishing_ || !prepared_ || attempted_){error="Controller creation state invalid; inspect retained transaction before retry";return false;}
        struct Guard{bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(publishing_);
        attempted_=true;
        try{
            Revalidate();Publication::Rename(metadata_->handle,next_->metadata);metadataPublished_=true;
            if(afterMetadata)afterMetadata();CheckRoot();Verify(*metadata_,next_->metadata,next_->metadataBytes,next_->metadataIdentity,16384);
            Verify(*source_,sourceStage_,next_->bytes,next_->sourceIdentity,AnimationSystem::Controller::TextCodec::MaxBytes);
            Publication::Rename(source_->handle,next_->source);published_=true;error.clear();return true;
        }catch(const std::exception& exception){
            error=exception.what();if(metadataPublished_ && !published_){try{Publication::Rename(metadata_->handle,metadataStage_);metadataPublished_=false;}catch(...){error+="; owned metadata retained at destination; stage occupied/inaccessible";}}
            if(!directory_.empty())error+="; retained Controller creation: "+Base::PathToUtf8(directory_);return false;
        }
    }
};
}
