#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <stdexcept>

namespace Engine::Base {
// Whole-file replacement for cooperating writers; not conflict merging or a security sandbox.
class AtomicFile final {
public:
    static bool Write(const std::filesystem::path& path,const std::string& bytes,size_t limit,std::string& error,bool replaceExisting=true) {
        std::filesystem::path temporary;bool ownsTemporary=false;
        try {
            if(path.empty() || path.native().find(L'\0')!=std::wstring::npos) throw std::runtime_error("Destination path must be non-empty without embedded NUL");
            if(bytes.size()>limit || bytes.size()>MAXDWORD) throw std::runtime_error("File exceeds its size limit");
            const auto destination=std::filesystem::absolute(path).lexically_normal();
            std::filesystem::create_directories(destination.parent_path());
            static std::atomic<unsigned long long> sequence{0};
            temporary=destination;
            temporary+=L".kubo-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence)+L".tmp";
            const HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot exclusively create temporary file (Windows error "+std::to_string(GetLastError())+")");
            ownsTemporary=true;DWORD written=0;
            const bool success=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)!=FALSE && written==bytes.size() && FlushFileBuffers(file)!=FALSE;
            const bool closed=CloseHandle(file)!=FALSE;
            if(!success || !closed) throw std::runtime_error("Cannot flush and close temporary file");
            if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH|(replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0)))
                throw std::runtime_error("Cannot replace destination (Windows error "+std::to_string(GetLastError())+")");
            ownsTemporary=false;error.clear();return true;
        } catch(const std::exception& exception) {
            if(ownsTemporary) {std::error_code cleanup;std::filesystem::remove(temporary,cleanup);}
            error=exception.what();return false;
        }
    }
};
}
