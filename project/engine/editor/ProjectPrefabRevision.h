#pragma once
#include "ProjectPrefab.h"
#include <memory>

namespace Engine::Editor {
struct PrefabFileIdentity {
    DWORD volume=0,indexHigh=0,indexLow=0;
    bool operator==(const PrefabFileIdentity&) const = default;
};
struct ProjectPrefabRevision {
    std::filesystem::path file,metadata,finalFile,finalMetadata;
    std::string guid,bytes,metadataBytes;
    PrefabFileIdentity fileIdentity,metadataIdentity;
    PrefabValue definition;
};
// A read lease only. Both files deny writes/deletes while this object holds them.
// Releasing it before a rename does NOT provide compare-and-swap publication;
// a future writer must coordinate revision checks, replacement and history.
class HeldProjectPrefabRevision final {
    friend class HeldProjectPrefabWriter;
    friend class PrefabSourcePublication;
    friend class PrefabSourceRecovery;
    friend class PrefabSourceHistoryEffect;
    friend class PrefabSourceHistoryRecovery;
    friend class NativePrefabApplyEffect;
    struct NativeFile {
        HANDLE handle=INVALID_HANDLE_VALUE;
        explicit NativeFile(const std::filesystem::path& path,DWORD access=GENERIC_READ,DWORD creation=OPEN_EXISTING) {
            handle=CreateFileW(path.c_str(),access,FILE_SHARE_READ,nullptr,creation,
                FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
            if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Prefab source/identity is missing, busy or inaccessible");
        }
        ~NativeFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        NativeFile(const NativeFile&)=delete;
        NativeFile& operator=(const NativeFile&)=delete;
    };
public:
    HeldProjectPrefabRevision()=default;
    HeldProjectPrefabRevision(const HeldProjectPrefabRevision&)=delete;
    HeldProjectPrefabRevision& operator=(const HeldProjectPrefabRevision&)=delete;
    const ProjectPrefabRevision& Snapshot() const{return snapshot_;}
    void Release(){source_.reset();metadata_.reset();snapshot_={};}
    bool Open(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
        const std::string& guid,std::string& error,const ProjectPrefabRevision* expected=nullptr) {
        try {
            const auto* record=registry.FindByGuid(guid);
            if(!record || record->type!=Assets::AssetType::Prefab)throw std::runtime_error("Select a registered Prefab GUID");
            ProjectPrefabRevision candidate;candidate.guid=guid;
            candidate.file=ProjectPrefabDetail::Path(project,Base::PathFromUtf8(record->absolutePath));
            candidate.metadata=Assets::AssetRegistry::MetadataPath(candidate.file);
            auto source=std::make_unique<NativeFile>(candidate.file);
            auto metadata=std::make_unique<NativeFile>(candidate.metadata);
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            Read(*source,root,Scene::SceneDocument::MaxBytes,candidate.bytes,candidate.fileIdentity,candidate.finalFile);
            Read(*metadata,root,16384,candidate.metadataBytes,candidate.metadataIdentity,candidate.finalMetadata);
            // Keep BOTH handles while existing project/GUID/duplicate/schema checks
            // reread the same pathname. No missing metadata is manufactured.
            PrefabValue freshDefinition;
            if(!ReadProjectPrefab(project,registry,guid,freshDefinition,error))return false;
            const auto identityDocument=ParseHeldJson(candidate.metadataBytes);
            if(!identityDocument.is_object() || !identityDocument.contains("version") || !identityDocument["version"].is_number() ||
                identityDocument["version"].get<crude_json::number>()!=1 || !identityDocument.contains("guid") ||
                !identityDocument["guid"].is_string() || identityDocument["guid"].get<crude_json::string>()!=guid)
                throw std::runtime_error("Held Prefab metadata does not match the selected identity");
            if(!ValidatePrefabDocument(ParseHeldJson(candidate.bytes),candidate.definition,error))return false;
            if(freshDefinition.dump()!=candidate.definition.dump())throw std::runtime_error("Prefab changed during project validation");
            if(std::filesystem::canonical(candidate.file)!=candidate.finalFile ||
               std::filesystem::canonical(candidate.metadata)!=candidate.finalMetadata)
                throw std::runtime_error("Prefab pathname changed during read");
            if(expected && (expected->guid!=candidate.guid || expected->file!=candidate.file ||
                expected->metadata!=candidate.metadata || expected->finalFile!=candidate.finalFile ||
                expected->finalMetadata!=candidate.finalMetadata || expected->fileIdentity!=candidate.fileIdentity ||
                expected->metadataIdentity!=candidate.metadataIdentity || expected->bytes!=candidate.bytes ||
                expected->metadataBytes!=candidate.metadataBytes))throw std::runtime_error("Prefab source or identity changed externally; rebuild the Apply plan");
            source_=std::move(source);metadata_=std::move(metadata);snapshot_=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
private:
    static PrefabValue ParseHeldJson(std::string text) {
        if(text.find('\0')!=std::string::npos || (!text.empty() &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0))
            throw std::runtime_error("Invalid held Prefab JSON UTF-8");
        if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);
        size_t depth=0,structures=0;bool quoted=false,escaped=false;
        for(const char c:text) {
            if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='\"')quoted=false;}
            else if(c=='\"')quoted=true;
            else if(c=='{' || c=='['){if(++depth>128 || ++structures>65536)throw std::runtime_error("Held Prefab JSON structure budget exceeded");}
            else if(c=='}' || c==']'){if(!depth)throw std::runtime_error("Invalid held Prefab JSON structure");--depth;}
        }
        return PrefabValue::parse(text);
    }
    static void Read(const NativeFile& file,const std::filesystem::path& root,size_t limit,std::string& bytes,
        PrefabFileIdentity& identity,std::filesystem::path& finalPath) {
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(GetFileType(file.handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.handle,&info) ||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks!=1 ||
            (!info.nFileIndexHigh && !info.nFileIndexLow) ||
            !GetFileSizeEx(file.handle,&size) || size.QuadPart<0 || static_cast<unsigned long long>(size.QuadPart)>limit)
            throw std::runtime_error("Prefab revision requires bounded native single-link files");
        const auto required=GetFinalPathNameByHandleW(file.handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!required || required>32768)throw std::runtime_error("Cannot resolve held Prefab filename");
        std::wstring name(required,L'\0');
        const auto count=GetFinalPathNameByHandleW(file.handle,name.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!count || count>=required)throw std::runtime_error("Held Prefab filename changed");
        name.resize(count);
        if(name.starts_with(L"\\\\?\\UNC\\"))name=L"\\\\"+name.substr(8);
        else if(name.starts_with(L"\\\\?\\"))name.erase(0,4);
        finalPath=std::filesystem::path(name).lexically_normal();
        if(!ProjectPrefabDetail::Within(finalPath,root))throw std::runtime_error("Held Prefab resolves outside the asset root");
        identity={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};
        bytes.resize(static_cast<size_t>(size.QuadPart));DWORD read=0,extraRead=0;char extra=0;
        if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size() ||
            !ReadFile(file.handle,&extra,1,&extraRead,nullptr) || extraRead)
            throw std::runtime_error("Cannot read complete held Prefab revision");
    }
    std::unique_ptr<NativeFile> source_,metadata_;
    ProjectPrefabRevision snapshot_;
};
inline bool ReadProjectPrefabRevision(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
    const std::string& guid,ProjectPrefabRevision& output,std::string& error) {
    HeldProjectPrefabRevision lease;if(!lease.Open(project,registry,guid,error))return false;
    output=lease.Snapshot();error.clear();return true;
}
inline bool ValidateProjectPrefabRevision(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
    const ProjectPrefabRevision& expected,std::string& error) {
    HeldProjectPrefabRevision lease;return lease.Open(project,registry,expected.guid,error,&expected);
}
}
