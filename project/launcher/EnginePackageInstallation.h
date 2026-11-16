#pragma once
#include "EnginePackageVerification.h"
#include <cstring>
#include <type_traits>

namespace KuboHub {
struct InstalledPackageReport {
    std::filesystem::path directory;
    PackageVerificationReport package;
    bool alreadyPresent=false,journalRetained=false;
    void Swap(InstalledPackageReport& other) noexcept {
        directory.swap(other.directory);package.Swap(other.package);
        std::swap(alreadyPresent,other.alreadyPresent);std::swap(journalRetained,other.journalRetained);
    }
};
static_assert(std::is_nothrow_move_constructible_v<InstalledPackageReport>);
// Local, user-confirmed installation. Does not authenticate, register, select,
// launch, migrate projects, delete versions, or terminate running processes.
// A directory publication and catalog selection are deliberately separate commits.
class EnginePackageInstallation final {
    using V=EnginePackageVerification;
    using Handle=V::Handle;
    static constexpr uint64_t FreeReserve=uint64_t{4}<<30;
    static constexpr char Marker[]="kubo-managed-engines-1\n";
    static void Need(bool ok,const char* message){V::Need(ok,message);}
    static bool Hex(std::string_view text,size_t length){return text.size()==length && std::all_of(text.begin(),text.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});}
    static DWORD Attributes(const std::filesystem::path& path){
        const auto attrs=GetFileAttributesW(path.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES)return attrs;
        const auto error=GetLastError();Need(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND,"Managed store entry is inaccessible");return attrs;
    }
    static std::filesystem::path LocalPath(const std::filesystem::path& input){
        Need(input.is_absolute() && input.native().size()<=28000 && input.native().find(L'\0')==std::wstring::npos,"Managed store requires a bounded absolute path");
        auto path=input.lexically_normal();path.make_preferred();const auto drive=path.root_path().native();
        Need(drive.size()==3 && drive[1]==L':' && ((drive[0]>=L'A' && drive[0]<=L'Z') || (drive[0]>=L'a' && drive[0]<=L'z')),"Managed store must use a local drive-letter path");
        const auto type=GetDriveTypeW(path.root_path().c_str());Need(type==DRIVE_FIXED || type==DRIVE_REMOVABLE,"Managed store must be on a local drive");
        size_t count=0;for(const auto& piece:path.relative_path()){
            const auto part=piece.native();Need(++count<=128 && !part.empty() && part.back()!=L'.' && part.back()!=L' ',"Managed store has a normalized path alias");
            for(const auto c:part)Need(c>=32 && c!=127 && c!=L':' && c!=L'<' && c!=L'>' && c!=L'"' && c!=L'|' && c!=L'?' && c!=L'*',"Managed store has an unsupported path component");
        }
        Need(path!=path.root_path(),"A drive root cannot be a managed engine store");return path;
    }
    static bool Contains(const std::filesystem::path& parent,const std::filesystem::path& child){
        auto prefix=parent.native();if(prefix.back()!=L'\\' && prefix.back()!=L'/')prefix+=L'\\';
        const auto text=child.native();return CompareStringOrdinal(parent.c_str(),-1,child.c_str(),-1,TRUE)==CSTR_EQUAL ||
            (text.size()>prefix.size() && CompareStringOrdinal(prefix.data(),static_cast<int>(prefix.size()),text.data(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL);
    }
    static Handle Record(const std::filesystem::path& path,DWORD disposition,DWORD access){
        Handle file(CreateFileW(path.c_str(),access,FILE_SHARE_READ,nullptr,disposition,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        Need(file.value!=INVALID_HANDLE_VALUE,"Managed store record is busy or inaccessible");
        const auto info=V::Info(file.value);Need(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && info.nNumberOfLinks==1,"Managed store record must be a single-link ordinary file");
        V::Streams(path);return file;
    }
    static std::string Read(HANDLE file,size_t maximum){
        const auto size=V::Size(file);Need(size<=maximum,"Managed store record exceeds limit");
        LARGE_INTEGER zero{};Need(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN),"Cannot seek managed store record");
        std::string bytes(static_cast<size_t>(size),'\0');DWORD read=0,extra=0;char probe=0;
        Need(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==bytes.size() && ReadFile(file,&probe,1,&extra,nullptr) && extra==0,"Cannot read complete managed store record");return bytes;
    }
    static void Write(HANDLE file,const std::string& bytes){
        DWORD written=0;Need(bytes.size()<=MAXDWORD && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file),"Cannot flush managed store record");
    }
    struct Store {
        std::filesystem::path root;std::vector<Handle> ancestors;Handle lock,marker;
    };
    static bool Empty(const std::filesystem::path& root,bool allowLock){
        struct Find {HANDLE value=INVALID_HANDLE_VALUE;~Find(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}} find;
        WIN32_FIND_DATAW data{};find.value=FindFirstFileW((root/L"*").c_str(),&data);
        if(find.value==INVALID_HANDLE_VALUE){Need(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot enumerate managed store");return true;}
        for(;;){
            const std::wstring name=data.cFileName;
            if(name!=L"." && name!=L".." && !(allowLock && name==L".install.lock"))return false;
            if(!FindNextFileW(find.value,&data)){Need(GetLastError()==ERROR_NO_MORE_FILES,"Cannot finish store enumeration");return true;}
        }
    }
    static Store OpenStore(const std::filesystem::path& path,bool create){
        Store store;store.root=LocalPath(path);auto ancestor=store.root.root_path();store.ancestors.push_back(V::Open(ancestor,true));
        for(const auto& part:store.root.relative_path()){
            ancestor/=part;
            if(Attributes(ancestor)==INVALID_FILE_ATTRIBUTES){Need(create,"Managed store does not exist");Need(CreateDirectoryW(ancestor.c_str(),nullptr),"Cannot exclusively create managed store directory");}
            store.ancestors.push_back(V::Open(ancestor,true));
        }
        const auto marker=store.root/L"store-format.txt";
        if(Attributes(marker)==INVALID_FILE_ATTRIBUTES)Need(create && Empty(store.root,false),"Refusing to adopt a nonempty unmanaged directory");
        store.lock=Record(store.root/L".install.lock",OPEN_ALWAYS,GENERIC_READ|GENERIC_WRITE);
        Need(V::Size(store.lock.value)==0,"Managed store lock must be empty");
        if(Attributes(marker)==INVALID_FILE_ATTRIBUTES){
            Need(create && Empty(store.root,true),"Managed store changed during initialization");
            store.marker=Record(marker,CREATE_NEW,GENERIC_READ|GENERIC_WRITE);Write(store.marker.value,Marker);
        }else {store.marker=Record(marker,OPEN_EXISTING,GENERIC_READ);Need(Read(store.marker.value,128)==Marker,"Unsupported managed store marker");}
        return store;
    }
    static std::string Token(){
        std::array<unsigned char,16> random{};Need(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"Cannot generate private installation identity");
        std::string token;token.reserve(32);constexpr char hex[]="0123456789abcdef";for(auto byte:random){token+=hex[byte>>4];token+=hex[byte&15];}return token;
    }
    static std::filesystem::path Candidate(const Store& store,const std::string& token){return store.root/(L".pending-"+V::Native(token).native());}
    static std::filesystem::path Final(const Store& store,const std::string& hash){return store.root/(L"package-"+V::Native(hash).native());}
    static std::filesystem::path Journal(const Store& store,const std::string& token){return store.root/(L"pending-"+V::Native(token).native()+L".json");}
    static std::string JournalBytes(const std::string& token,const std::string& hash){
        crude_json::value value(crude_json::object{});value["format"]="kubo-engine-install-1";value["token"]=token;value["manifestSha256"]=hash;return value.dump();
    }
    static std::string JournalHash(HANDLE file,const std::string& token){
        const auto bytes=Read(file,4096);std::string error;
        Need(Engine::Base::BoundedJson::Validate(bytes,{4096,4,32},error),"Invalid installation journal JSON");
        const auto value=crude_json::value::parse(bytes);
        Need(value.is_object() && value.get<crude_json::object>().size()==3 && value.contains("format") && value.contains("token") && value.contains("manifestSha256"),"Invalid installation journal fields");
        for(const auto* key:{"format","token","manifestSha256"})Need(value[key].is_string(),"Invalid installation journal field type");
        Need(value["format"].get<crude_json::string>()=="kubo-engine-install-1" && value["token"].get<crude_json::string>()==token,"Installation journal identity mismatch");
        auto hash=value["manifestSha256"].get<crude_json::string>();Need(Hex(hash,64),"Invalid installation manifest digest");return hash;
    }
    static bool Retire(HANDLE journal) noexcept {FILE_DISPOSITION_INFO info{TRUE};return SetFileInformationByHandle(journal,FileDispositionInfo,&info,sizeof(info))!=FALSE;}
    static InstalledPackageReport Existing(const std::filesystem::path& path,const std::string& hash,
                                           const std::atomic_bool* cancel,std::atomic<uint64_t>* progress){
        std::string error;auto source=V::Acquire(path/L"engine-package.json",error,{},cancel,progress);
        Need(source.report.manifestSha256==hash,"Occupied version directory does not match requested package");
        InstalledPackageReport result;result.directory=path;result.package.Swap(source.report);result.alreadyPresent=true;return result;
    }
    static void Copy(HANDLE source,const std::filesystem::path& destination,uint64_t size,const std::string& hash,
                     const std::atomic_bool* cancel,std::atomic<uint64_t>* progress){
        PackageDetail::CheckCancel(cancel);
        auto target=Record(destination,CREATE_NEW,GENERIC_READ|GENERIC_WRITE);
        LARGE_INTEGER zero{};Need(SetFilePointerEx(source,zero,nullptr,FILE_BEGIN),"Cannot seek held package source");
        std::array<unsigned char,64*1024> buffer{};uint64_t total=0;
        for(;;){
            PackageDetail::CheckCancel(cancel);DWORD read=0,written=0;
            Need(ReadFile(source,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr),"Cannot read held package source");if(!read)break;
            Need(read<=size-total,"Held source exceeds expected byte count");total+=read;
            Need(WriteFile(target.value,buffer.data(),read,&written,nullptr) && written==read,"Cannot copy complete package file");
            if(progress)progress->fetch_add(read,std::memory_order_relaxed);
        }
        Need(total==size && FlushFileBuffers(target.value),"Cannot flush complete candidate file");
        Need(PackageDetail::Digest(target.value,size,cancel,progress)==hash,"Copied candidate SHA-256 mismatch");
    }
    static InstalledPackageReport Publish(Store& store,const std::string& token,const std::string& hash,HANDLE journal,
                                           const std::atomic_bool* cancel,std::atomic<uint64_t>* progress){
        const auto candidate=Candidate(store,token),destination=Final(store,hash);
        // The anchor permits our later DELETE access, but denies reparse writes.
        Handle anchor(CreateFileW(candidate.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        Need(anchor.value!=INVALID_HANDLE_VALUE,"Cannot hold candidate directory");const auto identity=V::Info(anchor.value);
        Need(identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY,"Candidate is not a directory");
        InstalledPackageReport result;
        {std::string error;auto held=V::Acquire(candidate/L"engine-package.json",error,{},cancel,progress);
            Need(held.report.manifestSha256==hash,"Candidate manifest differs from durable journal");
            result.directory=destination;result.package.Swap(held.report);}
        // No children are held across the Windows directory rename. The anchored
        // root identity is rechecked; same-user hostile child writers are not a sandbox guarantee.
        Handle publish(CreateFileW(candidate.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        Need(publish.value!=INVALID_HANDLE_VALUE,"Candidate is busy before publication");const auto now=V::Info(publish.value);
        Need(identity.dwVolumeSerialNumber==now.dwVolumeSerialNumber && identity.nFileIndexHigh==now.nFileIndexHigh && identity.nFileIndexLow==now.nFileIndexLow,"Candidate directory identity changed");
        const auto name=destination.native();const auto bytes=name.size()*sizeof(wchar_t);
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO)+bytes,0);auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->ReplaceIfExists=FALSE;rename->FileNameLength=static_cast<DWORD>(bytes);std::memcpy(rename->FileName,name.data(),bytes);
        PackageDetail::CheckCancel(cancel);
        Need(SetFileInformationByHandle(publish.value,FileRenameInfo,rename,static_cast<DWORD>(buffer.size())),"Cannot publish candidate without overwriting an existing version; pending installation retained");
        // Commit point: all owning result fields were prepared. Do not report a
        // failed install after the rename because journal cleanup failed.
        result.journalRetained=!Retire(journal);return result;
    }
public:
    enum class Result {Installed,AlreadyPresent,Invalid,Cancelled};
    static Result Install(const std::filesystem::path& manifestPath,const std::filesystem::path& storePath,
                          InstalledPackageReport& output,std::string& error,const std::atomic_bool* cancel=nullptr,
                          std::atomic<uint64_t>* progress=nullptr,uint64_t minimumFreeBytes=FreeReserve){
        try{
            const auto root=LocalPath(storePath),sourceRoot=LocalPath(manifestPath.parent_path());
            Need(!Contains(root,sourceRoot) && !Contains(sourceRoot,root),"Package source and managed store must not overlap");
            Need(minimumFreeBytes>=FreeReserve,"Installation free-space reserve cannot be lowered below 4 GiB");
            auto source=V::Acquire(manifestPath,error,{},cancel,progress);PackageDetail::CheckCancel(cancel);
            auto store=OpenStore(root,true);const auto destination=Final(store,source.report.manifestSha256);
            if(Attributes(destination)!=INVALID_FILE_ATTRIBUTES){auto result=Existing(destination,source.report.manifestSha256,cancel,progress);output.Swap(result);error.clear();return Result::AlreadyPresent;}
            ULARGE_INTEGER available{};
            Need(GetDiskFreeSpaceExW(store.root.c_str(),&available,nullptr,nullptr) && available.QuadPart>=source.report.totalBytes && available.QuadPart-source.report.totalBytes>=minimumFreeBytes,"Insufficient installation space; require package bytes plus at least 4 GiB reserve");
            const auto token=Token();auto journal=Record(Journal(store,token),CREATE_NEW,GENERIC_READ|GENERIC_WRITE|DELETE);
            Write(journal.value,JournalBytes(token,source.report.manifestSha256));
            const auto candidate=Candidate(store,token);Need(CreateDirectoryW(candidate.c_str(),nullptr),"Cannot exclusively create private candidate; journal retained");
            std::vector<Handle> directories;directories.push_back(V::Open(candidate,true));
            for(const auto& entry:source.work.inventory)if(entry.second){PackageDetail::CheckCancel(cancel);const auto path=candidate/entry.first;Need(CreateDirectoryW(path.c_str(),nullptr),"Cannot exclusively create candidate subdirectory");directories.push_back(V::Open(path,true));}
            Copy(source.manifest.value,candidate/L"engine-package.json",V::Size(source.manifest.value),source.report.manifestSha256,cancel,progress);
            for(const auto& file:source.work.files)Copy(file.file.value,candidate/file.relative,file.size,source.work.expected.at(file.relative),cancel,progress);
            directories.clear();auto result=Publish(store,token,source.report.manifestSha256,journal.value,cancel,progress);
            output.Swap(result);error.clear();return Result::Installed;
        }catch(const PackageDetail::Cancelled&){error="Installation cancelled; current versions/selection unchanged; any private candidate/journal retained";return Result::Cancelled;}
        catch(const std::exception& e){error=e.what();return Result::Invalid;}
    }
    // Explicit recovery only. Never deletes partial candidates or invents missing
    // source bytes. Complete candidates are verified again before publication.
    static Result Recover(const std::filesystem::path& storePath,const std::string& token,InstalledPackageReport& output,
                          std::string& error,const std::atomic_bool* cancel=nullptr,std::atomic<uint64_t>* progress=nullptr){
        try{
            PackageDetail::CheckCancel(cancel);Need(Hex(token,32),"Recovery token must be 32 lowercase hex characters");
            auto store=OpenStore(storePath,false);auto journal=Record(Journal(store,token),OPEN_EXISTING,GENERIC_READ|DELETE);
            const auto hash=JournalHash(journal.value,token);const auto candidate=Candidate(store,token),destination=Final(store,hash);
            if(Attributes(destination)!=INVALID_FILE_ATTRIBUTES){
                Need(Attributes(candidate)==INVALID_FILE_ATTRIBUTES,"Both candidate and installed version exist; preserve both for inspection");
                auto result=Existing(destination,hash,cancel,progress);PackageDetail::CheckCancel(cancel);result.journalRetained=!Retire(journal.value);
                output.Swap(result);error.clear();return Result::AlreadyPresent;
            }
            Need(Attributes(candidate)!=INVALID_FILE_ATTRIBUTES,"Journal has no complete candidate; retry from source, journal retained");
            auto result=Publish(store,token,hash,journal.value,cancel,progress);output.Swap(result);error.clear();return Result::Installed;
        }catch(const PackageDetail::Cancelled&){error="Recovery cancelled; private candidate/journal retained";return Result::Cancelled;}
        catch(const std::exception& e){error=e.what();return Result::Invalid;}
    }
};
}
