#pragma once
#include "AnimatorControllerRecovery.h"
#include <atomic>

namespace Engine::Editor {
// Explicit source-only acknowledgement. Archive the complete transaction,
// never delete it; the caller still owns document/World coordination.
class AnimatorControllerAcknowledgement final {
    friend class AnimatorControllerCreationRecovery;
    friend class ControllerRecordInspection;
    struct Directory {
        HANDLE handle=INVALID_HANDLE_VALUE;
        Directory(const std::filesystem::path& path,const std::filesystem::path& root){
            handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot own Controller transaction directory");
            try{
                BY_HANDLE_FILE_INFORMATION info{};
                if(!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || (!info.nFileIndexHigh && !info.nFileIndexLow))throw std::runtime_error("Controller directory not ordinary");
                const auto required=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
                if(!required || required>32768)throw std::runtime_error("Cannot resolve Controller directory");
                std::wstring name(required,L'\0');const auto count=GetFinalPathNameByHandleW(handle,name.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
                if(!count || count>=required)throw std::runtime_error("Controller directory changed");name.resize(count);
                if(name.starts_with(L"\\\\?\\UNC\\"))name=L"\\\\"+name.substr(8);else if(name.starts_with(L"\\\\?\\"))name.erase(0,4);
                const auto final=std::filesystem::path(name).lexically_normal();
                if(final!=std::filesystem::canonical(path) || !AnimatorControllerPublication::Within(final,root))throw std::runtime_error("Controller directory escaped/changed");
            }catch(...){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;throw;}
        }
        ~Directory(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        Directory(const Directory&)=delete;Directory& operator=(const Directory&)=delete;
    };
public:
    static bool ArchiveResolved(const Base::ProjectContext& project,const std::string& guid,const ControllerRecoveryReport& expected,std::filesystem::path& archived,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}){
        try{
            if(!project.IsOpen() || guid.empty() || !AnimationSystem::AnimatorComponent::ValidControllerGuid(guid))throw std::runtime_error("Acknowledgement requires project/Controller GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!AnimatorControllerPublication::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Acknowledgement root escaped");
            Directory directory(root/".kubo/controller-transactions"/guid,root);
            std::filesystem::path destination;ControllerRecoveryReport unused;
            const bool success=AnimatorControllerRecovery::Run(project,guid,unused,error,&expected,[&](const ControllerRecoveryReport& report,const std::function<void()>& revalidate,const std::function<void()>& releaseChildren){
                if(report.state!=ControllerRecoveryState::Prepared && report.state!=ControllerRecoveryState::Published)throw std::runtime_error("Restore Parked/Conflict Controller before acknowledgement");
                const auto parent=root/".kubo/controller-archive";
                if(!AnimatorControllerPublication::Within(std::filesystem::weakly_canonical(parent),root))throw std::runtime_error("Controller archive root escaped");
                std::filesystem::create_directories(parent);const auto attributes=GetFileAttributesW(parent.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !AnimatorControllerPublication::Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Controller archive root linked/changed");
                static std::atomic<unsigned long long> sequence{0};
                destination=parent/(guid+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
                if(beforeArchive)beforeArchive(destination);
                revalidate();
                // Retain directory + writer lock + source + metadata. Windows
                // requires child handles closed for directory rename. This is
                // cooperating-writer exclusion, not a hostile child-name sandbox.
                releaseChildren();AnimatorControllerPublication::Rename(directory.handle,destination);
            });
            if(success)archived.swap(destination);return success;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
