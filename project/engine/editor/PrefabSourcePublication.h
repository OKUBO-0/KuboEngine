#pragma once
#include "ProjectPrefabWriter.h"
#include <functional>
#include <vector>

namespace Engine::Editor {
// Source publication only. A retained journal/backup is intentionally NOT
// acknowledged/deleted here: a caller must coordinate World/history and recovery.
class PrefabSourcePublication final {
    friend class PrefabSourceRecovery;
    friend class PrefabSourceAcknowledgement;
    friend class PrefabSourceHistoryEffect;
    friend class PrefabSourceHistoryRecovery;
    friend class NativePrefabApplyEffect;
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
public:
    PrefabSourcePublication()=default;
    PrefabSourcePublication(const PrefabSourcePublication&)=delete;
    PrefabSourcePublication& operator=(const PrefabSourcePublication&)=delete;
    const std::filesystem::path& Directory() const{return directory_;}
    bool IsPublished() const{return published_;}
    bool Prepare(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
        const ProjectPrefabRevision& expected,const PrefabValue& nextSource,std::string& error) {
        try {
            if(prepared_ || !directory_.empty())throw std::runtime_error("Source publication object already used");
            PrefabValue normalized;if(!ValidatePrefabDocument(nextSource,normalized,error))return false;
            if(!writer_.Open(project,registry,expected,error))return false;
            if(normalized["root"].get<crude_json::number>()!=writer_.Snapshot().definition["root"].get<crude_json::number>())throw std::runtime_error("Source publication cannot change root identity");
            const auto parent=writer_.root_/".kubo/prefab-transactions";
            if(!ProjectPrefabDetail::Within(std::filesystem::weakly_canonical(parent),writer_.root_))throw std::runtime_error("Source transaction parent escapes asset root");
            std::filesystem::create_directories(parent);
            const auto parentAttributes=GetFileAttributesW(parent.c_str());
            if(parentAttributes==INVALID_FILE_ATTRIBUTES || (parentAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                !ProjectPrefabDetail::Within(std::filesystem::canonical(parent),writer_.root_))throw std::runtime_error("Source transaction parent is linked or changed");
            const auto directory=parent/expected.guid;
            if(!std::filesystem::create_directory(directory))throw std::runtime_error("Source transaction directory occupied; recovery required");
            directory_=directory;source_=writer_.Snapshot().finalFile;backup_=directory/"original.prefab";stage_=directory/"next.prefab";
            const auto bytes=normalized.dump();
            PrefabFileIdentity createdIdentity;std::filesystem::path createdFinal;
            {NativeFile output(stage_,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);
                std::string empty;HeldProjectPrefabRevision::Read(output,writer_.root_,0,empty,createdIdentity,createdFinal);
                if(createdFinal!=std::filesystem::canonical(stage_))throw std::runtime_error("New staged filename resolves elsewhere");
                Write(output.handle,bytes);
            }
            stageFile_=std::make_unique<NativeFile>(stage_,GENERIC_READ|DELETE);
            std::string stagedBytes;PrefabFileIdentity stagedIdentity;std::filesystem::path stagedFinal;
            HeldProjectPrefabRevision::Read(*stageFile_,writer_.root_,Scene::SceneDocument::MaxBytes,stagedBytes,stagedIdentity,stagedFinal);
            if(stagedIdentity!=createdIdentity || stagedFinal!=createdFinal || stagedBytes!=bytes || stagedFinal!=std::filesystem::canonical(stage_))throw std::runtime_error("Staged source changed before journal");
            PrefabValue record;record["format"]="kubo-prefab-source-publication-1";record["guid"]=expected.guid;
            record["sourceRelative"]=Base::PathToUtf8(source_.lexically_relative(writer_.root_));
            record["metadataBytes"]=expected.metadataBytes;
            record["originalBytes"]=writer_.snapshot_.bytes;record["nextBytes"]=bytes;
            record["originalIdentity"]=Identity(writer_.snapshot_.fileIdentity);
            record["metadataIdentity"]=Identity(writer_.snapshot_.metadataIdentity);record["nextIdentity"]=Identity(stagedIdentity);
            // Immutable intent. Recovery must infer prepare/park/publish from
            // exact native identities, never a mutable status string alone.
            journal_=std::make_unique<NativeFile>(directory/"pending.json",GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);
            const auto journalBytes=record.dump();if(journalBytes.size()>20*1024*1024)throw std::runtime_error("Source journal exceeds 20 MiB");
            Write(journal_->handle,journalBytes);
            if(!writer_.Revalidate(error))return false;
            prepared_=true;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Publish(std::string& error,const std::function<void()>& afterPark={}) {
        try {
            if(!prepared_ || published_ || parked_)throw std::runtime_error("Source publication is not in prepared state");
            if(!writer_.Revalidate(error))return false;
            // Own handle + no replacement: never reopen an unprotected source
            // name to decide which file gets moved to the backup.
            Rename(writer_.source_->handle,backup_);parked_=true;
            if(afterPark)afterPark();
            Rename(stageFile_->handle,source_);published_=true;error.clear();return true;
        }catch(const std::exception& exception) {
            error=exception.what();
            if(parked_ && !published_) {
                try {Rename(writer_.source_->handle,source_);parked_=false;}
                catch(const std::exception&){error+="; original backup retained because rollback destination is occupied/inaccessible";}
            }
            if(!directory_.empty())error+="; pending source transaction retained: "+Base::PathToUtf8(directory_);
            return false;
        }
    }
private:
    static PrefabValue Identity(const PrefabFileIdentity& identity) {
        PrefabValue value;value["volume"]=double(identity.volume);value["high"]=double(identity.indexHigh);value["low"]=double(identity.indexLow);return value;
    }
    static void Write(HANDLE file,const std::string& bytes) {
        DWORD written=0;
        if(bytes.size()>MAXDWORD || !WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size() || !FlushFileBuffers(file))
            throw std::runtime_error("Cannot flush owned source transaction file");
    }
    static void Rename(HANDLE file,const std::filesystem::path& destination) {
        const auto name=std::filesystem::absolute(destination).lexically_normal().native();
        const auto size=offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t);
        if(size>MAXDWORD)throw std::runtime_error("Source rename path too large");
        std::vector<unsigned char> storage(size,0);auto* info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
        std::copy(name.begin(),name.end(),info->FileName);
        if(!SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(size)))throw std::runtime_error("Owned no-replace source rename failed (Windows error "+std::to_string(GetLastError())+")");
    }
    HeldProjectPrefabWriter writer_;
    std::unique_ptr<NativeFile> stageFile_,journal_;
    std::filesystem::path directory_,source_,backup_,stage_;
    bool prepared_=false,parked_=false,published_=false;
};
}
