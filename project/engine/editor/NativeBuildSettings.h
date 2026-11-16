#pragma once
#include "EditorSettingsFile.h"
#include <crude_json.h>

namespace Engine::Editor {
struct NativeBuildSettings {
    bool native = false, debug = false;
    std::string sdk, project, output = "Builds/Export";
};

// Exact loaded document, including absence and unknown fields. Not an ABI/API token.
struct NativeBuildSettingsSnapshot {
    std::filesystem::path path;
    std::string bytes;
    bool exists = false;
};

// Local authoring preferences only. Loading never executes a project/toolchain.
class NativeBuildSettingsFile final {
public:
    static constexpr size_t Limit = 16384;
    static bool Load(const std::filesystem::path& path, NativeBuildSettings& settings, std::string& error) {
        NativeBuildSettingsSnapshot snapshot;
        return Load(path,settings,snapshot,error);
    }
    static bool Load(const std::filesystem::path& path, NativeBuildSettings& settings,
        NativeBuildSettingsSnapshot& snapshot, std::string& error) {
        try {
            auto document = ReadDocument(path);
            const auto value = Parse(document);
            const auto candidate = Decode(value);
            settings = candidate; snapshot = std::move(document); error.clear(); return true;
        } catch(const std::exception& e) {error=e.what();return false;}
    }
    static bool Save(const std::filesystem::path& path, const NativeBuildSettings& settings, std::string& error) {
        return SaveDocument(path,settings,nullptr,nullptr,error);
    }
    static bool SaveChecked(const std::filesystem::path& path, const NativeBuildSettings& settings,
        NativeBuildSettingsSnapshot& snapshot, std::string& error) {
        return SaveDocument(path,settings,&snapshot,&snapshot,error);
    }
private:
    struct OwnedHandle {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~OwnedHandle() {if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
        OwnedHandle(const OwnedHandle&)=delete;
        OwnedHandle& operator=(const OwnedHandle&)=delete;
        explicit OwnedHandle(HANDLE handle):value(handle) {}
    };
    static bool SaveDocument(const std::filesystem::path& path,const NativeBuildSettings& settings,
        const NativeBuildSettingsSnapshot* expected,NativeBuildSettingsSnapshot* result,std::string& error) {
        try {
            Validate(settings);
            const auto destination=std::filesystem::absolute(path).lexically_normal();
            if(expected && expected->path!=destination) throw std::runtime_error("Reload local build settings for this project before saving");
            std::filesystem::create_directories(destination.parent_path());
            auto lockPath=destination;lockPath+=L".lock";
            // Persistent zero-byte sidecar: deleting it after unlock would race a
            // second writer opening a different file at the same pathname.
            OwnedHandle lock(CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
                nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            if(lock.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Native settings save is busy or its lock is inaccessible; retry later");
            BY_HANDLE_FILE_INFORMATION lockInfo{};
            if(!GetFileInformationByHandle(lock.value,&lockInfo) ||
                (lockInfo.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || lockInfo.nFileSizeHigh || lockInfo.nFileSizeLow)
                throw std::runtime_error("Native settings lock must be a native empty file");
            auto document=ReadDocument(destination);
            if(expected && (document.exists!=expected->exists || document.bytes!=expected->bytes))
                throw std::runtime_error("Native build settings changed externally; draft retained. Reload local build settings before saving");
            auto value = Parse(document); // Preserve unknown fields; refuse malformed existing settings.
            Decode(value);
            value["version"] = 1.0; value["native"] = settings.native; value["debug"] = settings.debug;
            value["sdk"] = settings.sdk; value["project"] = settings.project; value["output"] = settings.output;
            auto bytes=value.dump(2);
            if(!Engine::Base::AtomicFile::Write(destination,bytes,Limit,error)) return false;
            if(result) {document.bytes=std::move(bytes);document.exists=true;*result=std::move(document);}
            return true;
        } catch(const std::exception& e) {error=e.what();return false;}
    }
    static NativeBuildSettingsSnapshot ReadDocument(const std::filesystem::path& path) {
        NativeBuildSettingsSnapshot document;document.path=std::filesystem::absolute(path).lexically_normal();
        OwnedHandle input(CreateFileW(document.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(input.value==INVALID_HANDLE_VALUE) {
            const auto code=GetLastError();
            if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND) return document;
            throw std::runtime_error("Cannot read native build settings (Windows error "+std::to_string(code)+")");
        }
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(input.value,&size) || size.QuadPart<0 || size.QuadPart>Limit)
            throw std::runtime_error("Cannot size native build settings or file exceeds 16 KiB");
        document.bytes.resize(static_cast<size_t>(size.QuadPart));DWORD read=0,extraRead=0;char extra=0;
        if(!ReadFile(input.value,document.bytes.data(),static_cast<DWORD>(document.bytes.size()),&read,nullptr) ||
            read!=document.bytes.size() || !ReadFile(input.value,&extra,1,&extraRead,nullptr) || extraRead)
            throw std::runtime_error("Native build settings changed while reading or cannot be read");
        document.exists=true;return document;
    }
    static crude_json::value Parse(const NativeBuildSettingsSnapshot& document) {
        if(!document.exists) return crude_json::object{};
        auto text=document.bytes;
        if(text.find('\0')!=std::string::npos || (!text.empty() &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0))
            throw std::runtime_error("Invalid UTF-8 native build settings");
        if(text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);
        size_t depth=0;bool quoted=false,escaped=false;
        for(const char c:text) {
            if(quoted) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='\"') quoted=false;}
            else if(c=='\"') quoted=true;
            else if(c=='{' || c=='[') {if(++depth>32) throw std::runtime_error("Native build settings nesting exceeds 32");}
            else if((c=='}' || c==']') && depth) --depth;
        }
        auto value=crude_json::value::parse(text);
        if(!value.is_object()) throw std::runtime_error("Native build settings must be a JSON object");
        return value;
    }
    static NativeBuildSettings Decode(const crude_json::value& value) {
        if(value.contains("version") && (!value["version"].is_number() || value["version"].get<crude_json::number>()!=1))
            throw std::runtime_error("Unsupported native build settings version");
        NativeBuildSettings result;
        for(const auto key:{"native","debug"}) if(value.contains(key)) {
            if(!value[key].is_boolean()) throw std::runtime_error("Native build flags must be boolean");
            (std::string(key)=="native" ? result.native : result.debug)=value[key].get<crude_json::boolean>();
        }
        for(const auto key:{"sdk","project","output"}) if(value.contains(key)) {
            if(!value[key].is_string()) throw std::runtime_error("Native build paths must be strings");
            auto& field=std::string(key)=="sdk" ? result.sdk : std::string(key)=="project" ? result.project : result.output;
            field=value[key].get<crude_json::string>();
        }
        Validate(result);return result;
    }
    static void Validate(const NativeBuildSettings& settings) {
        for(const auto* field:{&settings.sdk,&settings.project,&settings.output}) {
            const auto limit=field==&settings.output ? size_t(1023) : size_t(4096);
            if(field->size()>limit || field->find('\0')!=std::string::npos || (!field->empty() &&
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,field->data(),static_cast<int>(field->size()),nullptr,0)==0))
                throw std::runtime_error("Invalid UTF-8 or overlong native build path");
        }
    }
};
}
