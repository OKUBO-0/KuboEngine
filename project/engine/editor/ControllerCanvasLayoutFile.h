#pragma once
#include "AnimatorControllerCanvasState.h"
#include "AnimatorControllerDocument.h"
#include "engine/animation/AnimatorComponent.h"
#include "engine/base/ProjectContext.h"
#include "engine/base/AtomicFile.h"
#include "engine/assets/AssetImportFingerprint.h"

namespace Engine::Editor {
struct ControllerCanvasLayoutSnapshot {
    std::filesystem::path path;
    std::string bytes;
    bool exists=false;
    void Swap(ControllerCanvasLayoutSnapshot& other) noexcept {path.swap(other.path);bytes.swap(other.bytes);std::swap(exists,other.exists);}
};
// Local preferences only. Exact canonical graph binding permits index-based disk
// coordinates to be remapped to a NEW document's authoring ID epoch safely.
class ControllerCanvasLayoutFile final {
    // Project-wide Controller preferences share only the checked native IO;
    // their schema and lifetime remain independent of graph layout documents.
    friend class ControllerEditorPreferencesFile;
    using Canvas=AnimatorControllerCanvasState;using Kind=Canvas::Kind;using V=crude_json::value;
    struct Handle {
        HANDLE value=INVALID_HANDLE_VALUE;
        explicit Handle(HANDLE h):value(h){}
        ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
        Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    };
    static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
    static void Ordinary(const Handle& file,bool directory) {
        BY_HANDLE_FILE_INFORMATION info{};
        Need(file.value!=INVALID_HANDLE_VALUE && GetFileType(file.value)==FILE_TYPE_DISK && GetFileInformationByHandle(file.value,&info) &&
            !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory &&
            (info.nFileIndexHigh || info.nFileIndexLow) && (directory || info.nNumberOfLinks==1),"Layout requires ordinary native directories and single-link files");
    }
    struct Directories {
        std::vector<std::unique_ptr<Handle>> held;
        bool complete=true;
        Directories(const Base::ProjectContext& project,bool create) {
            Need(project.IsOpen(),"Layout requires an open project");
            Base::ProjectContext fresh;std::string error;Need(fresh.Open(project.GetProjectFile(),error),"Layout project manifest unavailable");
            Need(fresh.GetRootPath()==project.GetRootPath() && std::filesystem::canonical(project.GetRootPath())==project.GetRootPath(),"Layout project root changed");
            auto path=project.GetRootPath();
            for(int i=0;i<3;++i){
                if(i)path/=i==1?L"UserSettings":L"ControllerLayouts";
                const auto attr=GetFileAttributesW(path.c_str());
                if(attr==INVALID_FILE_ATTRIBUTES){
                    const auto code=GetLastError();Need(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND,"Layout directory inaccessible");
                    if(!create){complete=false;return;}
                    std::filesystem::create_directory(path);
                }
                // Allocate ownership BEFORE acquiring a native handle. Passing
                // CreateFileW directly to make_unique leaks it if allocation fails.
                auto handle=std::make_unique<Handle>(INVALID_HANDLE_VALUE);
                handle->value=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                Ordinary(*handle,true);
                const auto required=GetFinalPathNameByHandleW(handle->value,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);Need(required && required<=32768,"Cannot resolve layout directory");
                std::wstring name(required,L'\0');const auto count=GetFinalPathNameByHandleW(handle->value,name.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);Need(count && count<required,"Layout directory changed");name.resize(count);
                if(name.starts_with(L"\\\\?\\UNC\\"))name=L"\\\\"+name.substr(8);else if(name.starts_with(L"\\\\?\\"))name.erase(0,4);
                Need(std::filesystem::path(name).lexically_normal()==path,"Layout directory escaped/changed");held.push_back(std::move(handle));
            }
        }
    };
    static std::filesystem::path Path(const Base::ProjectContext& project,const std::string& guid) {
        Need(project.IsOpen() && !guid.empty() && AnimationSystem::AnimatorComponent::ValidControllerGuid(guid),"Layout requires a valid project/Controller GUID");
        return project.GetRootPath()/L"UserSettings/ControllerLayouts"/(guid+".json");
    }
    static ControllerCanvasLayoutSnapshot Read(const std::filesystem::path& path,size_t limit=Limit) {
        ControllerCanvasLayoutSnapshot result;result.path=path;
        Handle input(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(input.value==INVALID_HANDLE_VALUE){const auto code=GetLastError();Need(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND,"Layout file inaccessible or busy");return result;}
        Ordinary(input,false);LARGE_INTEGER size{};
        Need(GetFileSizeEx(input.value,&size) && size.QuadPart>=0 && static_cast<unsigned long long>(size.QuadPart)<=limit,"Controller preferences exceed byte limit or cannot be sized");
        result.bytes.resize(static_cast<size_t>(size.QuadPart));DWORD count=0,extra=0;char byte=0;
        Need(ReadFile(input.value,result.bytes.data(),static_cast<DWORD>(result.bytes.size()),&count,nullptr) && count==result.bytes.size() && ReadFile(input.value,&byte,1,&extra,nullptr) && !extra,"Layout read changed or failed");
        result.exists=true;return result;
    }
    static std::string Hash(const std::string& text) {
        Assets::FingerprintDetail::Hash hash;hash.Open();hash.Field(text);return hash.Finish();
    }
    static void Fields(const V& value,std::initializer_list<const char*> keys) {
        Need(value.is_object() && value.get<crude_json::object>().size()==keys.size(),"Unknown/missing layout fields");
        for(const auto key:keys)Need(value.contains(key),"Missing layout field");
    }
    static size_t Index(const V& value,size_t limit) {
        Need(value.is_number(),"Layout index must be numeric");const auto number=value.get<crude_json::number>();
        Need(std::isfinite(number) && number>=0 && number<std::numeric_limits<size_t>::max() && std::floor(number)==number && number<static_cast<double>(limit),"Layout index out of bounds");return static_cast<size_t>(number);
    }
    static Canvas::Point Point(const V& value) {
        Need(value.is_array() && value.get<crude_json::array>().size()==2 && value[0].is_number() && value[1].is_number(),"Layout point must contain two numbers");
        const auto x=value[0].get<crude_json::number>(),y=value[1].get<crude_json::number>();
        Need(std::isfinite(x) && std::isfinite(y) && std::abs(x)<=10000000 && std::abs(y)<=10000000,"Invalid layout coordinates");
        return {static_cast<float>(x),static_cast<float>(y)};
    }
    static V PointValue(Canvas::Point point){return crude_json::array{double(point.x),double(point.y)};}
    static V Parse(const std::string& bytes) {
        std::string error;Need(bytes.size()<=Limit && AnimationSystem::Controller::TextCodec::ValidateJson(bytes,error),"Invalid/bounded/duplicate layout JSON");
        auto value=V::parse(bytes);Fields(value,{"format","project","guid","graph","layers","selection"});
        for(const auto key:{"format","project","guid","graph"})Need(value[key].is_string(),"Layout binding must be text");
        Need(value["format"].get<crude_json::string>()=="kubo-controller-layout-1","Unsupported layout version");
        Need(!value["guid"].get<crude_json::string>().empty() && AnimationSystem::AnimatorComponent::ValidControllerGuid(value["guid"].get<crude_json::string>()),"Invalid layout GUID");
        const auto& hash=value["graph"].get<crude_json::string>();Need(hash.size()==64 && std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid layout graph digest");
        Need(value["layers"].is_array() && !value["layers"].get<crude_json::array>().empty() && value["layers"].get<crude_json::array>().size()<=4,"Invalid layout layers");
        size_t total=0;
        for(const auto& layer:value["layers"].get<crude_json::array>()){
            Fields(layer,{"any","states"});Point(layer["any"]);
            Need(layer["states"].is_array() && !layer["states"].get<crude_json::array>().empty() && layer["states"].get<crude_json::array>().size()<=64,"Invalid layout states");
            total+=layer["states"].get<crude_json::array>().size();Need(total<=128,"Too many layout states");
            for(const auto& point:layer["states"].get<crude_json::array>())Point(point);
        }
        Fields(value["selection"],{"layer","state"});const auto layer=Index(value["selection"]["layer"],value["layers"].get<crude_json::array>().size());
        Index(value["selection"]["state"],value["layers"][layer]["states"].get<crude_json::array>().size());return value;
    }
    static bool Matches(const V& value,const Base::ProjectContext& project,const std::string& guid,const AnimatorControllerDocument& document) {
        return value["project"].get<crude_json::string>()==Base::PathToUtf8(project.GetProjectFile()) && value["guid"].get<crude_json::string>()==guid && value["graph"].get<crude_json::string>()==Hash(document.Text());
    }
public:
    static constexpr size_t Limit=64*1024;
    // Missing/stale valid preferences are not errors and never apply guessed data.
    // On any malformed/IO/allocation error BOTH output state and receipt survive.
    static bool Load(const Base::ProjectContext& project,const std::string& guid,const AnimatorControllerDocument& document,Canvas& output,ControllerCanvasLayoutSnapshot& snapshot,bool& applied,std::string& error) {
        try{
            Need(document.Loaded(),"Layout requires a loaded Controller");
            const auto path=Path(project,guid);Directories directories(project,false);ControllerCanvasLayoutSnapshot receipt;receipt.path=path;if(directories.complete)receipt=Read(path);
            bool matched=false;Canvas candidate;
            if(receipt.exists){
                const auto value=Parse(receipt.bytes);matched=Matches(value,project,guid,document);
                if(matched){
                    const auto& ids=document.AuthoringIds();Need(value["layers"].get<crude_json::array>().size()==ids.layers.size(),"Layout graph shape mismatch");
                    for(size_t i=0;i<ids.layers.size();++i){const auto& layer=value["layers"][i];Need(layer["states"].get<crude_json::array>().size()==ids.layers[i].states.size(),"Layout state shape mismatch");
                        Need(candidate.Store(Canvas::Key(Kind::AnyNode,ids.layers[i].id),Point(layer["any"])),"Invalid layout Any State");
                        for(size_t j=0;j<ids.layers[i].states.size();++j)Need(candidate.Store(Canvas::Key(Kind::StateNode,ids.layers[i].states[j]),Point(layer["states"][j])),"Invalid layout state");
                    }
                    const auto layer=Index(value["selection"]["layer"],ids.layers.size());const auto state=Index(value["selection"]["state"],ids.layers[layer].states.size());candidate.Select(ids,layer,state);
                }
            }
            if(matched)output.Swap(candidate);snapshot.Swap(receipt);applied=matched;error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
    static bool SaveChecked(const Base::ProjectContext& project,const std::string& guid,const AnimatorControllerDocument& document,const Canvas& canvas,ControllerCanvasLayoutSnapshot& snapshot,std::string& error) {
        try{
            Need(document.Loaded() && !document.IsDirty(),"Save the Controller source before saving its layout");
            const auto path=Path(project,guid);Need(snapshot.path==path,"Load layout preferences before saving");
            V value;value["format"]="kubo-controller-layout-1";value["project"]=Base::PathToUtf8(project.GetProjectFile());value["guid"]=guid;value["graph"]=Hash(document.Text());value["layers"]=crude_json::array{};
            const auto& ids=document.AuthoringIds();
            for(const auto& layer:ids.layers){V l;l["any"]=PointValue(canvas.Position(Canvas::Key(Kind::AnyNode,layer.id),{}));l["states"]=crude_json::array{};
                for(size_t i=0;i<layer.states.size();++i)l["states"].push_back(PointValue(canvas.Position(Canvas::Key(Kind::StateNode,layer.states[i]),{180.f+float(i%4)*210,float(i/4)*130})));value["layers"].push_back(l);}
            auto selection=canvas;size_t layer=0,state=0;selection.Resolve(ids,layer,state);value["selection"]["layer"]=double(layer);value["selection"]["state"]=double(state);
            auto bytes=value.dump();Parse(bytes);Directories directories(project,true);
            auto lockPath=path;lockPath+=L".lock";Handle lock(CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Ordinary(lock,false);
            LARGE_INTEGER lockSize{};Need(GetFileSizeEx(lock.value,&lockSize) && lockSize.QuadPart==0,"Layout writer lock must be empty");
            auto current=Read(path);Need(current.exists==snapshot.exists && current.bytes==snapshot.bytes,"Layout changed externally; reload preferences before saving");
            if(current.exists)Parse(current.bytes);
            current.bytes.swap(bytes);current.exists=true;
            if(!Base::AtomicFile::Write(path,current.bytes,Limit,error))return false;
            snapshot.Swap(current);error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
};
}
