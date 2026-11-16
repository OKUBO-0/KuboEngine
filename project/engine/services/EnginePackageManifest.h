#pragma once
#include "../base/BoundedJson.h"
#include <charconv>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace Engine::Services {
struct EnginePackageFile {std::string path,sha256;};
// Package declarations, NOT verified payload hashes, signature or ABI evidence.
struct EnginePackageManifest {
    static constexpr size_t MaxBytes=1024*1024,MaxFiles=4096;
    std::string format,engineVersion,platform,bytes;
    std::vector<EnginePackageFile> files;
    bool Versioned() const noexcept{return format=="kubo-portable-engine-2";}
    void Swap(EnginePackageManifest& other) noexcept {format.swap(other.format);engineVersion.swap(other.engineVersion);platform.swap(other.platform);bytes.swap(other.bytes);files.swap(other.files);}
    static bool ValidVersion(std::string_view version) noexcept {
        if(version.empty() || version.size()>128)return false;
        size_t start=0;
        while(start<version.size()){
            const auto end=version.find('.',start),count=end==version.npos?version.size()-start:end-start;if(!count)return false;
            const auto part=version.substr(start,count);for(char c:part)if(c<'0' || c>'9')return false;
            int number=0;const auto parsed=std::from_chars(part.data(),part.data()+part.size(),number);if(parsed.ec!=std::errc{} || parsed.ptr!=part.data()+part.size())return false;
            if(end==version.npos)return true;start=end+1;if(start==version.size())return false;
        }return false;
    }
private:
    using V=crude_json::value;
    static void Need(bool ok){if(!ok)throw std::invalid_argument("Invalid or unsupported engine package declaration");}
    struct WindowsLess {bool operator()(const std::wstring& a,const std::wstring& b) const noexcept {return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;}};
    static void Fields(const V& value,std::initializer_list<const char*> fields){
        Need(value.is_object() && value.get<crude_json::object>().size()==fields.size());
        for(const auto* name:fields)Need(value.contains(name));
    }
    static const std::string& Text(const V& value){Need(value.is_string());return value.get<crude_json::string>();}
    static bool Hash(std::string_view value){return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');});}
    static std::wstring Path(std::string_view value){
        Need(!value.empty() && value.size()<=1024);
        for(const unsigned char c:value)Need(c>=32 && c!=127 && c!='\\' && c!=':' && c!='<' && c!='>' && c!='"' && c!='|' && c!='?' && c!='*');
        size_t start=0;
        while(start<value.size()){
            const auto end=value.find('/',start),count=end==value.npos?value.size()-start:end-start;Need(count>0);
            const auto part=value.substr(start,count);Need(part!="." && part!=".." && part.back()!='.' && part.back()!=' ');
            auto stem=std::string(part.substr(0,part.find('.')));for(auto& c:stem)if(c>='a' && c<='z')c-=32;
            Need(stem!="CON" && stem!="PRN" && stem!="AUX" && stem!="NUL" && stem!="CONIN$" && stem!="CONOUT$" && stem!="CLOCK$");
            Need(!(stem.size()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3]>='1' && stem[3]<='9'));
            Need(stem!="COM\xc2\xb9" && stem!="COM\xc2\xb2" && stem!="COM\xc2\xb3" && stem!="LPT\xc2\xb9" && stem!="LPT\xc2\xb2" && stem!="LPT\xc2\xb3");
            if(end==value.npos)break;start=end+1;Need(start<value.size());
        }
        auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));Need(!path.has_root_path());return path.native();
    }
public:
    static bool Decode(std::string_view input,EnginePackageManifest& output,std::string& error){
        try{
            Need(input.size()<=MaxBytes);auto text=input;if(text.starts_with("\xEF\xBB\xBF"))text.remove_prefix(3);
            if(!Base::BoundedJson::Validate(text,{MaxBytes,8,32768},error))return false;
            const auto root=V::parse(std::string(text));Need(root.is_object() && root.contains("format"));EnginePackageManifest next;
            next.format=Text(root["format"]);Need(next.format=="kubo-portable-engine-1" || next.Versioned());
            if(next.Versioned()){
                Fields(root,{"format","launcher","editor","player","configuration","files","engineVersion","platform"});
                next.engineVersion=Text(root["engineVersion"]);next.platform=Text(root["platform"]);
                Need(ValidVersion(next.engineVersion) && next.platform=="Windows-x64");
            }else Fields(root,{"format","launcher","editor","player","configuration","files"});
            Need(Text(root["launcher"])=="KuboLauncher.exe" && Text(root["editor"])=="KuboEditor.exe" && Text(root["player"])=="KuboPlayer.exe" && Text(root["configuration"])=="Release");
            Need(root["files"].is_array());const auto& entries=root["files"].get<crude_json::array>();Need(!entries.empty() && entries.size()<=MaxFiles);
            std::set<std::wstring,WindowsLess> seen;
            for(const auto& entry:entries){
                Fields(entry,{"path","sha256"});EnginePackageFile file{Text(entry["path"]),Text(entry["sha256"])};Need(Hash(file.sha256));
                const auto native=Path(file.path);Need(CompareStringOrdinal(native.c_str(),-1,L"engine-package.json",-1,TRUE)!=CSTR_EQUAL && seen.insert(native).second);
                for(auto& c:file.sha256)if(c>='A' && c<='F')c+=32;next.files.push_back(std::move(file));
            }
            for(const auto* required:{L"KuboLauncher.exe",L"KuboEditor.exe",L"KuboPlayer.exe"})Need(seen.contains(required));
            next.bytes=std::string(input);output.Swap(next);error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
    enum class ReadResult {Success,Missing,Invalid};
    static ReadResult Read(const std::filesystem::path& path,EnginePackageManifest& output,std::string& error){
        struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file;
        try{
            Need(path.is_absolute() && path.native().size()<=32767 && path.native().find(L'\0')==std::wstring::npos);
            file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(file.value==INVALID_HANDLE_VALUE){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND){error.clear();return ReadResult::Missing;}throw std::runtime_error("Engine package metadata is inaccessible");}
            BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
            Need(GetFileType(file.value)==FILE_TYPE_DISK && GetFileInformationByHandle(file.value,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 && GetFileSizeEx(file.value,&size) && size.QuadPart>=0 && static_cast<unsigned long long>(size.QuadPart)<=MaxBytes);
            std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0,extra=0;char probe=0;
            Need(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) && count==bytes.size() && ReadFile(file.value,&probe,1,&extra,nullptr) && !extra);
            return Decode(bytes,output,error)?ReadResult::Success:ReadResult::Invalid;
        }catch(const std::exception& e){error=e.what();return ReadResult::Invalid;}
    }
};
}
