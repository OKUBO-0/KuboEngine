#pragma once
#include "PrefabSourceRecovery.h"
#include <atomic>

namespace Engine::Editor {
// Explicit source-only acknowledgement. The caller still owns Scene/history
// coordination. Archive, never delete, the complete resolved transaction.
class PrefabSourceAcknowledgement final {
    friend class PrefabSourceHistoryEffect;
    friend class PrefabSourceHistoryRecovery;
    friend class NativePrefabApplyEffect;
    struct Directory {
        HANDLE handle=INVALID_HANDLE_VALUE;
        Directory(const std::filesystem::path& path,const std::filesystem::path& root){
            handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot own Prefab transaction directory");
            try {
                BY_HANDLE_FILE_INFORMATION info{};
                if(!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || (!info.nFileIndexHigh && !info.nFileIndexLow))
                    throw std::runtime_error("Prefab transaction directory is not ordinary native storage");
                const auto required=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
                if(!required || required>32768)throw std::runtime_error("Cannot resolve Prefab transaction directory");
                std::wstring name(required,L'\0');const auto count=GetFinalPathNameByHandleW(handle,name.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
                if(!count || count>=required)throw std::runtime_error("Prefab transaction directory path changed");name.resize(count);
                if(name.starts_with(L"\\\\?\\UNC\\"))name=L"\\\\"+name.substr(8);else if(name.starts_with(L"\\\\?\\"))name.erase(0,4);
                const auto final=std::filesystem::path(name).lexically_normal();
                if(final!=std::filesystem::canonical(path) || !ProjectPrefabDetail::Within(final,root))throw std::runtime_error("Prefab transaction directory escaped/changed");
            }catch(...){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;throw;}
        }
        ~Directory(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        Directory(const Directory&)=delete;Directory& operator=(const Directory&)=delete;
    };
public:
    static bool ArchiveResolved(const Base::ProjectContext& project,const std::string& guid,
        const PrefabSourceRecoveryReport& expected,std::filesystem::path& archived,std::string& error,
        const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        PrefabSourceRecoveryReport unused;std::filesystem::path destination;std::unique_ptr<Directory> directory;
        const bool success=PrefabSourceRecovery::InspectHeld(project,guid,unused,error,false,
            [&](const PrefabSourceRecoveryReport& current,const PrefabSourceRecovery::Observed&,const PrefabSourceRecovery::Observed&,const PrefabSourceRecovery::Observed&,const std::function<void()>& revalidate,const std::function<void()>& releaseTransactionReaders){
                if(!PrefabSourceRecovery::Same(current,expected) ||
                   (current.state!=PrefabSourceRecoveryState::Prepared && current.state!=PrefabSourceRecoveryState::Published))
                    throw std::runtime_error("Only revalidated Prepared/Published Prefab transactions can be acknowledged");
                const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
                const auto parent=root/".kubo/prefab-archive";
                if(!ProjectPrefabDetail::Within(std::filesystem::weakly_canonical(parent),root))throw std::runtime_error("Prefab archive parent escapes asset root");
                std::filesystem::create_directories(parent);const auto attributes=GetFileAttributesW(parent.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !ProjectPrefabDetail::Within(std::filesystem::canonical(parent),root))
                    throw std::runtime_error("Prefab archive parent changed/is linked");
                static std::atomic<unsigned long long> sequence{0};
                destination=parent/(guid+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
                if(beforeArchive)beforeArchive(destination);
                revalidate();
                // Windows directory rename requires its child file handles to
                // close. Keep the writer lock, source/metadata and owned directory
                // held; release only journal/backup/stage after final revalidation.
                // This is cooperating-writer exclusion, not a hostile child-name
                // race sandbox. Archive all contents, including unknown files.
                releaseTransactionReaders();
                PrefabSourcePublication::Rename(directory->handle,destination);
            },[&](const std::filesystem::path& path,const std::filesystem::path& root){directory=std::make_unique<Directory>(path,root);});
        if(success)archived=std::move(destination);return success;
    }
};
}
