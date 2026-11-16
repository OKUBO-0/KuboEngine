#pragma once
#include "engine/editor/NativeBuildSettings.h"
#include "engine/base/ProjectContext.h"
#include <vector>
#include <cstring>
#include <cstddef>
#include <charconv>

namespace Engine::Services {
// Local recovery record. No automatic deletion of interrupted stage/backup data.
class NativeCreateJournal final {
public:
    enum class Kind { Native, Project };
    enum class Recovery { None, Retained, Published, Attention };
    ~NativeCreateJournal() {if(file_!=INVALID_HANDLE_VALUE) CloseHandle(file_);}
    NativeCreateJournal()=default;
    NativeCreateJournal(const NativeCreateJournal&)=delete;
    NativeCreateJournal& operator=(const NativeCreateJournal&)=delete;
    static std::filesystem::path Path(const std::filesystem::path& root) {
        return root.parent_path()/(root.filename().wstring()+L".kubo-create-journal.json");
    }
    void Begin(const std::filesystem::path& root,const std::filesystem::path& manifest,
        const std::filesystem::path& stage,const std::filesystem::path& backup,Kind kind=Kind::Native) {
        file_=CreateFileW(Path(root).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE) throw std::runtime_error("Native creation journal exists or cannot be exclusively created");
        stageId_=Identity(stage);originalId_=Identity(root);
        const crude_json::value record=crude_json::object{{"format",Format(kind)},{"root",Engine::Base::PathToUtf8(root.filename())},
            {"manifest",Engine::Base::PathToUtf8(manifest.filename())},{"stage",Engine::Base::PathToUtf8(stage.filename())},
            {"backup",Engine::Base::PathToUtf8(backup.filename())},{"stageId",stageId_},{"originalId",originalId_}};
        const auto bytes=record.dump(2);DWORD written=0;
        if(bytes.size()>16384 || !WriteFile(file_,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size() || !FlushFileBuffers(file_))
            throw std::runtime_error("Cannot flush native creation recovery journal");
    }
    bool Discard() {
        if(file_==INVALID_HANDLE_VALUE) return true;
        FILE_DISPOSITION_INFO info{};info.DeleteFile=TRUE;
        return SetFileInformationByHandle(file_,FileDispositionInfo,&info,sizeof(info))!=FALSE;
    }
    void ValidateBeforePublication(const std::filesystem::path& root,const std::filesystem::path& stage) const {
        if(Identity(stage)!=stageId_ || Identity(root)!=originalId_) throw std::runtime_error("Native output/stage identity changed before publication");
    }
    void ValidateRetired(const std::filesystem::path& backup) const {
        if(Identity(backup)!=originalId_) throw std::runtime_error("Native original directory changed during retirement");
    }
    bool OwnsStage(const std::filesystem::path& stage) const {return !stageId_.empty() && Identity(stage)==stageId_;}
    static Recovery Recover(const std::filesystem::path& manifest,std::string& message,Kind kind=Kind::Native) {
        try {
            const auto root=manifest.parent_path(),journal=Path(root);
            if(!std::filesystem::exists(journal)) return Recovery::None;
            NativeCreateJournal held;
            held.file_=CreateFileW(journal.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(held.file_==INVALID_HANDLE_VALUE) throw std::runtime_error("Native creation is active or its journal is locked");
            BY_HANDLE_FILE_INFORMATION fileInfo{};LARGE_INTEGER size{};
            if(!GetFileInformationByHandle(held.file_,&fileInfo) || (fileInfo.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
                !GetFileSizeEx(held.file_,&size) || size.QuadPart<=0 || size.QuadPart>16384) throw std::runtime_error("Invalid native creation journal file");
            std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
            if(!ReadFile(held.file_,text.data(),static_cast<DWORD>(text.size()),&read,nullptr) || read!=text.size() || text.find('\0')!=std::string::npos ||
                !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)) throw std::runtime_error("Cannot read native creation journal UTF-8");
            size_t depth=0;bool quoted=false,escaped=false;
            for(char c:text) {
                if(quoted) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='\"') quoted=false;}
                else if(c=='\"') quoted=true;
                else if(c=='{' || c=='[') {if(++depth>16) throw std::runtime_error("Native journal nesting exceeds 16");}
                else if((c=='}' || c==']') && depth) --depth;
            }
            const auto record=crude_json::value::parse(text);
            const auto field=[&](const char* key) {
                if(!record.is_object() || !record.contains(key) || !record[key].is_string()) throw std::runtime_error("Invalid native journal schema");
                auto value=record[key].get<crude_json::string>();
                if(value.find('\0')!=std::string::npos) throw std::runtime_error("NUL in native journal");
                return value;
            };
            if(field("format")!=Format(kind)) throw std::runtime_error("Unsupported project creation journal format or template kind");
            const auto component=[&](const char* key) {
                const auto value=field(key);const auto path=Engine::Base::PathFromUtf8(value);
                if(value.empty() || value.find_first_of("\\/:<>\"|?*")!=std::string::npos || path.filename()!=path || path==L"." || path==L"..")
                    throw std::runtime_error("Invalid sibling path in native journal");
                return path;
            };
            if(_wcsicmp(component("root").c_str(),root.filename().c_str()) || _wcsicmp(component("manifest").c_str(),manifest.filename().c_str()))
                throw std::runtime_error("Native journal belongs to a different project; inspect it manually");
            const auto stageName=component("stage"),backupName=component("backup");
            const std::wstring prefix=kind==Kind::Native ? L".kubo-native-stage-" : L".kubo-project-stage-";
            if(!stageName.wstring().starts_with(prefix)) throw std::runtime_error("Invalid native journal stage prefix");
            const auto token=stageName.wstring().substr(prefix.size());
            if(token.empty() || token.find_first_not_of(L"0123456789-")!=std::wstring::npos || backupName.wstring()!=L".kubo-empty-backup-"+token)
                throw std::runtime_error("Invalid native journal sibling token");
            const auto stage=root.parent_path()/stageName,backup=root.parent_path()/backupName;
            const auto stageId=field("stageId"),originalId=field("originalId");
            if(!ValidIdentity(stageId) || (!originalId.empty() && !ValidIdentity(originalId))) throw std::runtime_error("Invalid directory identity in native journal");
            const auto currentStage=Identity(stage),currentRoot=Identity(root),currentBackup=Identity(backup);
            if((!currentStage.empty() && currentStage!=stageId) || (!currentBackup.empty() && (originalId.empty() || currentBackup!=originalId)))
                throw std::runtime_error("Native journal directory identity changed; recovery refused");
            Recovery result=Recovery::Attention;
            if(currentStage.empty() && currentRoot==stageId) {
                Engine::Base::ProjectContext completed;std::string error;
                if(!completed.Open(manifest,error)) throw std::runtime_error("Published native project cannot be reopened: "+error);
                result=Recovery::Published;message="Recovered already published native project";
            } else if(currentRoot.empty() && !originalId.empty() && currentBackup==originalId) {
                if(!std::filesystem::is_empty(backup) || !MoveFileExW(backup.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot restore original empty project folder without overwriting output");
                result=Recovery::Retained;message="Restored original empty folder; ";
                message+=currentStage.empty() ? "no interrupted stage remains." : "interrupted stage retained at "+Engine::Base::PathToUtf8(stage)+".";
                message+=" Retry creation after inspection.";
            } else if(currentBackup.empty() && currentRoot==originalId) {
                result=Recovery::Retained;message=currentStage.empty() ? "Interrupted creation left no stage. Retry creation." : "Interrupted creation stage retained at "+Engine::Base::PathToUtf8(stage)+". Retry creation after inspection.";
            } else throw std::runtime_error("Native creation recovery has conflicting output; all data retained");
            const auto archive=journal.wstring()+L".recovered-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
            const auto bytes=(archive.size()+1)*sizeof(wchar_t);
            std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO,FileName)+bytes,0);
            auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());rename->ReplaceIfExists=FALSE;rename->FileNameLength=static_cast<DWORD>(archive.size()*sizeof(wchar_t));
            std::memcpy(rename->FileName,archive.c_str(),bytes);
            if(!SetFileInformationByHandle(held.file_,FileRenameInfo,rename,static_cast<DWORD>(storage.size()))) throw std::runtime_error("Recovery record could not be archived; data retained");
            if(!currentBackup.empty() && result==Recovery::Published) message+="; empty-folder backup retained at "+Engine::Base::PathToUtf8(backup);
            return result;
        } catch(const std::exception& e) {message=e.what();return Recovery::Attention;}
    }
private:
    static const char* Format(Kind kind) noexcept {return kind==Kind::Native ? "kubo-native-create-1" : "kubo-project-create-1";}
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::string stageId_,originalId_;
    static bool ValidIdentity(const std::string& identity) {
        size_t start=0;unsigned long parts[3]{};
        for(int i=0;i<3;++i) {
            const auto separator=identity.find(':',start);
            const auto end=i==2 ? identity.size() : separator;
            if((i==2 && separator!=std::string::npos) || end==std::string::npos || end<=start || end-start>10) return false;
            const auto parsed=std::from_chars(identity.data()+start,identity.data()+end,parts[i]);
            if(parsed.ec!=std::errc{} || parsed.ptr!=identity.data()+end) return false;
            start=end+1;
        }
        return parts[1]!=0 || parts[2]!=0;
    }
    static std::string Identity(const std::filesystem::path& path) {
        const auto attrs=GetFileAttributesW(path.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES) {
            const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) return {};
            throw std::runtime_error("Cannot inspect native recovery directory");
        }
        if(!(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Native recovery path is not a native directory");
        const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read native recovery directory identity");
        BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info)!=FALSE;CloseHandle(handle);
        if(!ok || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || (!info.nFileIndexHigh && !info.nFileIndexLow)) throw std::runtime_error("Invalid native recovery directory identity");
        return std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string(info.nFileIndexHigh)+":"+std::to_string(info.nFileIndexLow);
    }
};
}
