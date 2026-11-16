#pragma once
#include <filesystem>
#include <string>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine::Assets {
namespace ControllerPendingDetail {
inline DWORD Attributes(const std::filesystem::path& path){
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Cannot inspect pending Controller storage");}
    return attributes;
}
inline bool Directory(const std::filesystem::path& path){
    const auto attributes=Attributes(path);if(attributes==INVALID_FILE_ATTRIBUTES)return false;
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Pending Controller storage is linked/not an ordinary directory");return true;
}
}
// Observation, not a directory-tree lock. Never follow a linked pending parent.
inline void AssertControllerResolved(const std::filesystem::path& root,const std::string& guid){
    if(guid.size()!=16 && guid.size()!=32)throw std::runtime_error("Invalid Controller pending GUID");
    for(const auto c:guid)if(!((c>='0' && c<='9') || (c>='a' && c<='f')))throw std::runtime_error("Invalid Controller pending GUID");
    const auto storage=root/".kubo";if(!ControllerPendingDetail::Directory(storage))return;
    for(const auto* kind:{"controller-transactions","controller-creations"}){
        const auto parent=storage/kind;if(!ControllerPendingDetail::Directory(parent))continue;
        if(ControllerPendingDetail::Attributes(parent/guid)!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Resolve pending Controller publication/creation before normal admission");
    }
}
// Export copies whole asset roots, so also refuse unreferenced/incomplete records.
inline void AssertControllerExportResolved(const std::filesystem::path& root){
    const auto storage=root/".kubo";if(!ControllerPendingDetail::Directory(storage))return;
    for(const auto* kind:{"controller-transactions","controller-creations"}){
        const auto parent=storage/kind;if(!ControllerPendingDetail::Directory(parent))continue;
        std::error_code error;auto iterator=std::filesystem::directory_iterator(parent,error);
        if(error)throw std::runtime_error("Cannot enumerate pending Controller storage for export");
        if(iterator!=std::filesystem::directory_iterator())throw std::runtime_error("Resolve all pending Controller records before export");
    }
}
}
