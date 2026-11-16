#pragma once
#include "engine/base/AtomicFile.h"
#include "externals/imgui_node_editor/crude_json.h"
#include <fstream>
#include <vector>
#include <utility>

namespace KuboHub {
struct EngineCatalog {
    std::vector<std::filesystem::path> editors;
    std::filesystem::path active; // Empty means the Editor beside this Hub.
    void Swap(EngineCatalog& other) noexcept {editors.swap(other.editors);active.swap(other.active);}
};
struct CatalogSnapshot {
    std::string bytes;bool exists=false;std::filesystem::path path;
    void Swap(CatalogSnapshot& other) noexcept {bytes.swap(other.bytes);std::swap(exists,other.exists);path.swap(other.path);}
};

// User-owned local executable choices, NOT version/ABI or signature verification.
class EngineCatalogFile final {
public:
    static constexpr size_t Limit=16384, Entries=32;
    static bool IsEditorPath(const std::filesystem::path& path) {
        if(!path.is_absolute() || path.native().size()>32767 || path.native().find(L'\0')!=std::wstring::npos) return false;
        const auto name=path.filename().wstring();
        return _wcsicmp(name.c_str(),L"KuboEditor.exe")==0 || _wcsicmp(name.c_str(),L"KuboEngine.exe")==0;
    }
    static bool Same(const std::filesystem::path& a,const std::filesystem::path& b) {
        return _wcsicmp(a.lexically_normal().c_str(),b.lexically_normal().c_str())==0;
    }
    static bool Load(const std::filesystem::path& file,EngineCatalog& catalog,CatalogSnapshot& snapshot,std::string& error) {
        try {auto candidate=Read(file);auto decoded=Decode(candidate);catalog.Swap(decoded);snapshot.Swap(candidate);error.clear();return true;}
        catch(const std::exception& e){error=e.what();return false;}
    }
    static bool SaveChecked(const std::filesystem::path& file,const EngineCatalog& catalog,CatalogSnapshot& snapshot,std::string& error) {
        HANDLE lock=INVALID_HANDLE_VALUE;
        try {
            Validate(catalog);
            if(snapshot.path!=std::filesystem::absolute(file).lexically_normal())throw std::runtime_error("Load this engine catalog before saving");
            std::filesystem::create_directories(file.parent_path());
            auto lockPath=file;lockPath+=L".lock";
            lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(lock==INVALID_HANDLE_VALUE) throw std::runtime_error("Engine catalog is busy or inaccessible; retry later");
            BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(lock,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nFileSizeHigh || info.nFileSizeLow)
                throw std::runtime_error("Engine catalog lock must be a native empty file");
            const auto current=Read(file);
            if(current.exists!=snapshot.exists || current.bytes!=snapshot.bytes) throw std::runtime_error("Engine catalog changed externally; restart Hub before saving");
            Decode(current); // Do not overwrite malformed settings.
            crude_json::value value(crude_json::array{});
            value.push_back("kubo-engine-catalog-1");value.push_back(Utf8(catalog.active));
            for(const auto& path:catalog.editors)value.push_back(Utf8(path));
            // Prepare every owning snapshot field before replacing the file.
            // A failed allocation must not report failure after committing disk.
            CatalogSnapshot next{value.dump(2),true,current.path};
            if(!Engine::Base::AtomicFile::Write(file,next.bytes,Limit,error)){CloseHandle(lock);return false;}
            snapshot.Swap(next);CloseHandle(lock);error.clear();return true;
        } catch(const std::exception& e){if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);error=e.what();return false;}
    }
private:
    static std::string Utf8(const std::filesystem::path& path) {
        const auto text=path.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());
    }
    static CatalogSnapshot Read(const std::filesystem::path& file) {
        CatalogSnapshot result;
        result.path=std::filesystem::absolute(file).lexically_normal();
        if(!std::filesystem::exists(file))return result;
        std::ifstream input(file,std::ios::binary);
        std::string text(Limit+1,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));
        const auto count=static_cast<size_t>(input.gcount());
        if(count>Limit || input.bad() || !input.eof())throw std::runtime_error("Cannot read engine catalog or it exceeds 16 KiB");
        text.resize(count);result.bytes=std::move(text);result.exists=true;return result;
    }
    static EngineCatalog Decode(const CatalogSnapshot& snapshot) {
        if(!snapshot.exists)return {};
        const auto& text=snapshot.bytes;
        if(text.empty() || text.find('\0')!=std::string::npos || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0))
            throw std::runtime_error("Engine catalog must be UTF-8 JSON");
        // The format is a flat string array, so no object keys or recursive data.
        bool quoted=false,escaped=false;size_t depth=0;
        for(const char c:text) {
            if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='\"')quoted=false;}
            else if(c=='\"')quoted=true;
            else if(c=='{' || c=='}' || c=='['){if(c!='[' || ++depth>1)throw std::runtime_error("Engine catalog must be a flat array");}
            else if(c==']' && depth)--depth;
        }
        const auto value=crude_json::value::parse(text);
        if(!value.is_array())throw std::runtime_error("Engine catalog must be an array");
        const auto& array=value.get<crude_json::array>();
        if(array.size()<2 || array.size()>Entries+2)throw std::runtime_error("Invalid engine catalog entry count");
        for(const auto& field:array)if(!field.is_string())throw std::runtime_error("Engine catalog fields must be strings");
        if(array[0].get<crude_json::string>()!="kubo-engine-catalog-1")throw std::runtime_error("Unsupported engine catalog format");
        const auto path=[](const crude_json::value& field){const auto& s=field.get<crude_json::string>();return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));};
        EngineCatalog result;result.active=path(array[1]);
        for(size_t i=2;i<array.size();++i)result.editors.push_back(path(array[i]));
        Validate(result);return result;
    }
    static void Validate(const EngineCatalog& catalog) {
        if(catalog.editors.size()>Entries)throw std::runtime_error("At most 32 external Editors can be registered");
        bool activeFound=catalog.active.empty();
        for(size_t i=0;i<catalog.editors.size();++i) {
            if(!IsEditorPath(catalog.editors[i]))throw std::runtime_error("Register an absolute KuboEditor.exe or KuboEngine.exe path");
            for(size_t j=0;j<i;++j)if(Same(catalog.editors[i],catalog.editors[j]))throw std::runtime_error("Duplicate Editor path");
            if(Same(catalog.active,catalog.editors[i]))activeFound=true;
        }
        if(!activeFound)throw std::runtime_error("Selected Editor is not registered");
    }
};
}
