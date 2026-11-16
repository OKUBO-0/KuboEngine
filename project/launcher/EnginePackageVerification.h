#pragma once
#include "PackageFileDigest.h"
#include "engine/services/EnginePackageManifest.h"
#include <map>
#include <set>
#include <utility>

namespace KuboHub {
// Informational result only. Handles are retired before return; this report is
// NOT an installation capability. An installer must verify while holding its
// own source handles and copy only those bytes into a private candidate.
struct PackageVerificationReport {
    std::string declaredVersion,manifestSha256;
    uint64_t totalBytes=0;size_t payloadFiles=0;
    void Swap(PackageVerificationReport& other) noexcept {
        declaredVersion.swap(other.declaredVersion);manifestSha256.swap(other.manifestSha256);
        std::swap(totalBytes,other.totalBytes);std::swap(payloadFiles,other.payloadFiles);
    }
};
struct PackageVerificationLimits {
    uint64_t fileBytes=uint64_t{1}<<30,totalBytes=uint64_t{4}<<30;
    size_t entries=8192,directories=4096;
};
class EnginePackageVerification final {
    using Manifest=Engine::Services::EnginePackageManifest;
    struct Less {
        bool operator()(const std::wstring& a,const std::wstring& b) const noexcept {
            return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;
        }
    };
    struct Handle {
        HANDLE value=INVALID_HANDLE_VALUE;
        Handle()=default;explicit Handle(HANDLE v):value(v){}
        Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
        Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
        Handle& operator=(Handle&& other) noexcept {
            if(this!=&other){Close();value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;
        }
        void Close() noexcept {if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
        ~Handle(){Close();}
    };
    static void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    static BY_HANDLE_FILE_INFORMATION Info(HANDLE file){
        BY_HANDLE_FILE_INFORMATION info{};
        Need(GetFileType(file)==FILE_TYPE_DISK && GetFileInformationByHandle(file,&info),"Cannot inspect package handle");
        // No offline/recall placeholders or reparse traversal, including cloud links.
        Need(!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE|0x00040000|0x00400000)),"Package contains a reparse/offline/recall entry");
        return info;
    }
    static Handle Open(const std::filesystem::path& path,bool directory){
        Need(path.native().size()<=32700,"Package path exceeds native path limit");
        Handle file(CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL),nullptr));
        Need(file.value!=INVALID_HANDLE_VALUE,"Package entry is busy or inaccessible");
        const auto info=Info(file.value);
        Need(bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory,"Package entry changed type");
        Need(directory || info.nNumberOfLinks==1,"Package file has multiple hard links");
        return file;
    }
    static uint64_t Size(HANDLE file){
        LARGE_INTEGER size{};Need(GetFileSizeEx(file,&size) && size.QuadPart>=0,"Cannot read package file size");
        return static_cast<uint64_t>(size.QuadPart);
    }
    static void Streams(const std::filesystem::path& path){
        struct Stream {HANDLE value=INVALID_HANDLE_VALUE;~Stream(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}} stream;
        WIN32_FIND_STREAM_DATA data{};stream.value=FindFirstStreamW(path.c_str(),FindStreamInfoStandard,&data,0);
        Need(stream.value!=INVALID_HANDLE_VALUE,"Cannot enumerate package file streams");
        for(;;){
            Need(wcscmp(data.cStreamName,L"::$DATA")==0,"Package file contains an undeclared alternate data stream");
            if(!FindNextStreamW(stream.value,&data)){Need(GetLastError()==ERROR_HANDLE_EOF,"Cannot finish package stream enumeration");break;}
        }
    }
    static std::filesystem::path Native(const std::string& utf8){
        auto result=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),utf8.size()));
        result.make_preferred();return result;
    }
    struct Payload {std::wstring relative;Handle file;uint64_t size;};
    struct Work {
        std::filesystem::path root;PackageVerificationLimits limits;
        const std::atomic_bool* cancel;std::vector<Handle> directories;
        std::vector<Payload> files;
        std::map<std::wstring,std::string,Less> expected;
        std::map<std::wstring,bool,Less> inventory;
        uint64_t total=0;size_t entries=0,dirCount=0;
        void AddBytes(uint64_t bytes,uint64_t fileLimit){
            Need(bytes<=fileLimit && bytes<=limits.totalBytes-total,"Package exceeds file or aggregate byte limit");total+=bytes;
        }
        void Scan(const std::filesystem::path& relative,size_t depth,bool final,
                  std::map<std::wstring,bool,Less>& observed){
            PackageDetail::CheckCancel(cancel);Need(depth<=64,"Package directory nesting exceeds limit");
            struct Find {HANDLE value=INVALID_HANDLE_VALUE;~Find(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}} find;
            WIN32_FIND_DATAW data{};const auto pattern=root/relative/L"*";
            find.value=FindFirstFileW(pattern.c_str(),&data);
            if(find.value==INVALID_HANDLE_VALUE){Need(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot enumerate package directory");return;}
            for(;;){
                PackageDetail::CheckCancel(cancel);
                if(wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L"..")){
                    Need(++entries<=limits.entries,"Package entry count exceeds limit");
                    const auto child=relative/data.cFileName;const auto name=child.native();
                    const std::wstring component=data.cFileName;
                    Need(!component.empty() && component.back()!=L'.' && component.back()!=L' ',"Package entry has a Win32-normalized alias");
                    for(const auto c:component)Need(c>=32 && c!=127 && c!=L':' && c!=L'\\' && c!=L'/' && c!=L'<' && c!=L'>' && c!=L'"' && c!=L'|' && c!=L'?' && c!=L'*',"Package entry has an unsupported name");
                    const bool directory=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
                    Need(observed.emplace(name,directory).second,"Package contains ordinal namespace aliases");
                    if(final){
                        const auto found=inventory.find(name);
                        Need(found!=inventory.end() && found->second==directory,"Package namespace changed during verification");
                    }else if(directory){
                        Need(++dirCount<=limits.directories,"Package directory count exceeds limit");
                        directories.push_back(Open(root/child,true));
                    }else if(CompareStringOrdinal(name.c_str(),-1,L"engine-package.json",-1,TRUE)!=CSTR_EQUAL){
                        Need(expected.contains(name),"Package contains an undeclared file");
                        auto file=Open(root/child,false);Streams(root/child);const auto bytes=Size(file.value);AddBytes(bytes,limits.fileBytes);
                        files.push_back({name,std::move(file),bytes});
                    }
                    if(directory)Scan(child,depth+1,final,observed);
                }
                if(!FindNextFileW(find.value,&data)){Need(GetLastError()==ERROR_NO_MORE_FILES,"Package enumeration failed");break;}
            }
        }
    };
    friend class EnginePackageInstallation;
    struct Source {
        Work work;Handle manifest;Manifest declaration;PackageVerificationReport report;
    };
    // Private move-only lease: installer copies from these same verified handles.
    static Source Acquire(const std::filesystem::path& manifestPath,std::string& error,
                          PackageVerificationLimits limits,const std::atomic_bool* cancel,
                          std::atomic<uint64_t>* progress){
            PackageDetail::CheckCancel(cancel);
            Need(limits.fileBytes>0 && limits.fileBytes<=(uint64_t{1}<<30) && limits.totalBytes>0 && limits.totalBytes<=(uint64_t{4}<<30) &&
                limits.entries>0 && limits.entries<=8192 && limits.directories<=4096,"Invalid package verification limits");
            Need(manifestPath.is_absolute() && manifestPath.native().find(L'\0')==std::wstring::npos && manifestPath.native().size()<=30000,"Package manifest requires a bounded absolute path");
            const auto path=manifestPath.lexically_normal();const auto drive=path.root_path().native();
            Need(drive.size()==3 && ((drive[0]>=L'A' && drive[0]<=L'Z') || (drive[0]>=L'a' && drive[0]<=L'z')) && drive[1]==L':' && (drive[2]==L'\\' || drive[2]==L'/'),"Only local drive-letter package paths are supported");
            const auto driveType=GetDriveTypeW(path.root_path().c_str());
            Need(driveType==DRIVE_FIXED || driveType==DRIVE_REMOVABLE,"Package source must be on a local fixed or removable drive");
            Need(CompareStringOrdinal(path.filename().c_str(),-1,L"engine-package.json",-1,TRUE)==CSTR_EQUAL,"Select engine-package.json");
            Source source{Work{path.parent_path(),limits,cancel}};auto& work=source.work;
            auto ancestor=path.root_path();work.directories.push_back(Open(ancestor,true));size_t count=0;
            for(const auto& component:work.root.relative_path()){
                Need(++count<=128,"Package ancestor depth exceeds limit");ancestor/=component;
                PackageDetail::CheckCancel(cancel);work.directories.push_back(Open(ancestor,true));
            }
            source.manifest=Open(path,false);auto& manifest=source.manifest;Streams(path);work.AddBytes(Size(manifest.value),Manifest::MaxBytes);
            auto& declaration=source.declaration;
            Need(Manifest::Read(path,declaration,error)==Manifest::ReadResult::Success,"Cannot decode package manifest");
            for(const auto& member:declaration.files)work.expected.emplace(Native(member.path).native(),member.sha256);
            work.Scan({},0,false,work.inventory);
            Need(work.files.size()==work.expected.size() && work.inventory.contains(L"engine-package.json"),"Package is missing declared files");
            auto& next=source.report;next.declaredVersion=declaration.engineVersion;
            next.manifestSha256=PackageDetail::Digest(manifest.value,Manifest::MaxBytes,cancel,progress);
            for(const auto& member:work.files){
                Need(PackageDetail::Digest(member.file.value,member.size,cancel,progress)==work.expected.at(member.relative),"Package payload SHA-256 mismatch");
                Need(Size(member.file.value)==member.size && Info(member.file.value).nNumberOfLinks==1,"Package payload identity changed");
            }
            work.entries=0;std::map<std::wstring,bool,Less> observed;work.Scan({},0,true,observed);
            Need(observed==work.inventory,"Package namespace changed during verification");
            Need(Info(manifest.value).nNumberOfLinks==1,"Package manifest identity changed");
            PackageDetail::CheckCancel(cancel);next.totalBytes=work.total;next.payloadFiles=work.files.size();
            return source;
    }
public:
    enum class Result {Success,Invalid,Cancelled};
    static Result Verify(const std::filesystem::path& manifestPath,PackageVerificationReport& output,std::string& error,
                         PackageVerificationLimits limits={},const std::atomic_bool* cancel=nullptr,
                         std::atomic<uint64_t>* progress=nullptr){
        try{
            auto source=Acquire(manifestPath,error,limits,cancel,progress);
            output.Swap(source.report);error.clear();return Result::Success;
        }catch(const PackageDetail::Cancelled&){error="Package verification cancelled";return Result::Cancelled;}
        catch(const std::exception& e){error=e.what();return Result::Invalid;}
    }
};
}
