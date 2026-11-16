#pragma once
#include "AnimatorControllerWriter.h"
#include <functional>

namespace Engine::Editor {
// Immutable intent/retained backup. Recovery and acknowledgement are separate;
// do not expose GUI Save until they and session-baseline publication are wired.
class AnimatorControllerPublication final {
    friend class AnimatorControllerRecovery;
    friend class AnimatorControllerAcknowledgement;
    friend class AnimatorControllerSession;
    friend class AnimatorControllerCreation;
    friend class AnimatorControllerCreationRecovery;
    friend class ControllerRecordInspection;
    using File=HeldAnimatorControllerWriter::File;
    using Revision=Assets::AnimatorControllerRevision;
    using V=crude_json::value;
    HeldAnimatorControllerWriter writer_;
    std::unique_ptr<File> stage_,journal_;
    std::shared_ptr<const Revision> next_;
    std::filesystem::path directory_,sourcePath_,stagePath_,backupPath_,journalPath_;
    Revision::Identity journalIdentity_;
    std::string journalBytes_;
    bool prepared_=false,parked_=false,published_=false,publishing_=false,attempted_=false;
    static bool Within(const std::filesystem::path& path,const std::filesystem::path& root){
        auto part=path.begin();for(const auto& component:root){if(part==path.end() || _wcsicmp(part->c_str(),component.c_str()))return false;++part;}return part!=path.end();
    }
    static V Identity(const Revision::Identity& id){V value;value["volume"]=double(id.volume);value["high"]=double(id.high);value["low"]=double(id.low);return value;}
    static void Write(HANDLE file,const std::string& bytes){DWORD written=0;if(bytes.size()>MAXDWORD || !WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size() || !FlushFileBuffers(file))throw std::runtime_error("Cannot flush Controller transaction file");}
    static void Rename(HANDLE file,const std::filesystem::path& destination){
        const auto name=std::filesystem::absolute(destination).lexically_normal().native();
        const auto size=offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t);if(size>MAXDWORD)throw std::runtime_error("Controller rename path too large");
        std::vector<unsigned char> buffer(size,0);auto* info=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));std::copy(name.begin(),name.end(),info->FileName);
        if(!SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(size)))throw std::runtime_error("Owned no-replace Controller rename failed: "+std::to_string(GetLastError()));
    }
    void RevalidatePrepared() const {
        if(!writer_.snapshot_ || !stage_ || !journal_ || !next_)throw std::runtime_error("Controller publication not prepared");
        if(std::filesystem::exists(writer_.root_/".kubo/asset-move.pending.json"))throw std::runtime_error("Asset move pending");
        HeldAnimatorControllerWriter::ReadOwned(*writer_.source_,*writer_.metadata_,writer_.root_,*writer_.snapshot_);
        std::string bytes;Revision::Identity identity;
        HeldAnimatorControllerWriter::ReadFile(*writer_.lock_,writer_.lockPath_,writer_.root_,0,bytes,identity);
        HeldAnimatorControllerWriter::ReadFile(*stage_,stagePath_,writer_.root_,AnimationSystem::Controller::TextCodec::MaxBytes,bytes,identity);
        if(identity!=next_->sourceIdentity || bytes!=next_->bytes)throw std::runtime_error("Controller stage changed");
        HeldAnimatorControllerWriter::ReadFile(*journal_,journalPath_,writer_.root_,2*1024*1024,bytes,identity);
        if(identity!=journalIdentity_ || bytes!=journalBytes_)throw std::runtime_error("Controller intent changed");
    }
public:
    bool IsPublished() const noexcept{return published_;}
    bool IsParked() const noexcept{return parked_;}
    const std::filesystem::path& Directory() const noexcept{return directory_;}
    const Revision* PublishedRevision() const noexcept{return published_?next_.get():nullptr;}
    bool Prepare(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,const Revision& expected,std::string_view nextText,std::string& error){
        try {
            if(prepared_ || !directory_.empty())throw std::runtime_error("Controller publication already used");
            auto next=std::make_shared<Revision>();
            if(!AnimationSystem::Controller::TextCodec::Decode(nextText,next->graph,error))return false;
            next->bytes=std::string(nextText);
            if(!writer_.Open(project,registry,expected,error))return false;
            const auto parent=writer_.root_/".kubo/controller-transactions";
            if(!Within(std::filesystem::weakly_canonical(parent),writer_.root_))throw std::runtime_error("Controller transaction parent escaped");
            std::filesystem::create_directories(parent);const auto attributes=GetFileAttributesW(parent.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Within(std::filesystem::canonical(parent),writer_.root_))throw std::runtime_error("Controller transaction parent linked/changed");
            const auto directory=parent/expected.guid;if(!std::filesystem::create_directory(directory))throw std::runtime_error("Controller transaction occupied; recovery needed");
            directory_=directory;sourcePath_=expected.source;stagePath_=directory/"next.source";backupPath_=directory/"original.source";journalPath_=directory/"pending.json";
            next->source=expected.source;next->metadata=expected.metadata;next->guid=expected.guid;next->metadataBytes=expected.metadataBytes;next->metadataIdentity=expected.metadataIdentity;
            {File output(stagePath_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);std::string empty;
                HeldAnimatorControllerWriter::ReadFile(output,stagePath_,writer_.root_,0,empty,next->sourceIdentity);Write(output.handle,next->bytes);}
            stage_=std::make_unique<File>(stagePath_,GENERIC_READ|DELETE);
            std::string staged;Revision::Identity identity;
            HeldAnimatorControllerWriter::ReadFile(*stage_,stagePath_,writer_.root_,AnimationSystem::Controller::TextCodec::MaxBytes,staged,identity);
            if(staged!=next->bytes || identity!=next->sourceIdentity)throw std::runtime_error("Controller stage changed before intent");
            Assets::FingerprintDetail::Hash hash;hash.Open();hash.Add(reinterpret_cast<const unsigned char*>(next->bytes.data()),static_cast<ULONG>(next->bytes.size()));next->contentHash=hash.Finish();
            V intent;intent["format"]="kubo-controller-source-publication-1";intent["guid"]=expected.guid;
            intent["sourceRelative"]=Base::PathToUtf8(sourcePath_.lexically_relative(writer_.root_));intent["originalBytes"]=writer_.snapshot_->bytes;intent["nextBytes"]=next->bytes;intent["metadataBytes"]=expected.metadataBytes;
            intent["originalIdentity"]=Identity(writer_.snapshot_->sourceIdentity);intent["nextIdentity"]=Identity(next->sourceIdentity);intent["metadataIdentity"]=Identity(expected.metadataIdentity);
            journalBytes_=intent.dump();if(journalBytes_.size()>2*1024*1024)throw std::runtime_error("Controller intent too large");
            journal_=std::make_unique<File>(journalPath_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);std::string empty;
            HeldAnimatorControllerWriter::ReadFile(*journal_,journalPath_,writer_.root_,0,empty,journalIdentity_);Write(journal_->handle,journalBytes_);
            next_=std::move(next);RevalidatePrepared();prepared_=true;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Publish(std::string& error,const std::function<void()>& afterPark={}){
        if(publishing_){error="Controller publication is already active";return false;}
        if(!prepared_ || published_ || parked_ || attempted_){error="Controller publication state invalid; recover before retry";return false;}
        struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(publishing_);
        try {
            attempted_=true;
            RevalidatePrepared();Rename(writer_.source_->handle,backupPath_);parked_=true;
            if(afterPark)afterPark();Rename(stage_->handle,sourcePath_);published_=true;error.clear();return true;
        }catch(const std::exception& exception){
            error=exception.what();if(parked_ && !published_){try{Rename(writer_.source_->handle,sourcePath_);parked_=false;}catch(const std::exception&){error+="; rollback destination occupied/inaccessible, original backup retained";}}
            if(!directory_.empty())error+="; retained Controller transaction: "+Base::PathToUtf8(directory_);return false;
        }
    }
};
}
