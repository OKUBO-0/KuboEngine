#pragma once
#include "ScriptProperty.h"
#include "ScriptPrefabWorkBudget.h"
#include "../assets/AssetRegistry.h"
#include "../base/AssetPath.h"
#include "../scene/prefab/PrefabDocument.h"
#include <set>

namespace Engine::Scripting {
// Runtime-thread read-only preflight. Registry must remain alive and immutable.
// Project-owned references are admitted, not GPU/VM/audio decoder success.
class ScriptPrefabAdmission final {
public:
    enum class Result { Success,Missing,Invalid,AllocationFailure,Limit };
    struct Reference {std::filesystem::path source;std::string hint,guid;};
private:
    using V=Prefab::PrefabValue;
    struct Refusal {Result result;};
    static void Need(bool ok,Result reason=Result::Invalid){if(!ok)throw Refusal{reason};}
    static Result MissingCode(DWORD code){return code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND?Result::Missing:Result::Invalid;}
    struct File {
        HANDLE value=INVALID_HANDLE_VALUE;
        ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
        File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
    };
    static void Checks(ScriptPrefabWorkBudget* budget,size_t count=1){if(budget)Need(budget->FileChecks(count),Result::Limit);}
    static void Open(File& file,const std::filesystem::path& path,DWORD access,ScriptPrefabWorkBudget* budget=nullptr){
        Checks(budget,3);
        file.value=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file.value==INVALID_HANDLE_VALUE)throw Refusal{MissingCode(GetLastError())};
        BY_HANDLE_FILE_INFORMATION info{};
        Need(GetFileType(file.value)==FILE_TYPE_DISK && GetFileInformationByHandle(file.value,&info) &&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1);
    }
    static std::string Read(const std::filesystem::path& path,size_t limit,ScriptPrefabWorkBudget* budget=nullptr){
        File file;Open(file,path,GENERIC_READ,budget);LARGE_INTEGER size{};Checks(budget);
        Need(GetFileSizeEx(file.value,&size) && size.QuadPart>=0 && static_cast<unsigned long long>(size.QuadPart)<=limit);
        // Reserve the bounded content extent and the extra EOF probe byte.
        if(budget)Need(budget->Bytes(static_cast<size_t>(size.QuadPart)+1),Result::Limit);
        Checks(budget,2);std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0,extra=0;char byte=0;
        Need(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) && count==bytes.size() && ReadFile(file.value,&byte,1,&extra,nullptr) && !extra);
        return bytes;
    }
    // Same strict object-key/number grammar needed by Controller admission,
    // but Prefabs retain their own 4MiB/64-depth bounds (not Controller's 256KiB).
    class JsonGate {
        std::string_view text_;size_t cursor_=0,nodes_=0;
        char Peek() const noexcept{return cursor_<text_.size()?text_[cursor_]:'\0';}
        bool Take(char c){if(Peek()!=c)return false;++cursor_;return true;}
        void Space(){while(Peek()==' ' || Peek()=='\t' || Peek()=='\r' || Peek()=='\n')++cursor_;}
        static bool Digit(char c){return c>='0' && c<='9';}
        std::string String(){
            const auto start=cursor_;Need(Take('"'));bool closed=false;
            while(cursor_<text_.size()){
                const auto c=static_cast<unsigned char>(text_[cursor_++]);Need(c>=32);
                if(c=='"'){closed=true;break;}
                if(c=='\\'){
                    Need(cursor_<text_.size());const auto escaped=text_[cursor_++];
                    Need(escaped=='"' || escaped=='\\' || escaped=='/' || escaped=='b' || escaped=='f' || escaped=='n' || escaped=='r' || escaped=='t' || escaped=='u');
                    if(escaped=='u')for(int i=0;i<4;++i){Need(cursor_<text_.size());const auto hex=text_[cursor_++];Need((hex>='0' && hex<='9') || (hex>='a' && hex<='f') || (hex>='A' && hex<='F'));}
                }
            }
            Need(closed);const auto decoded=V::parse(std::string(text_.substr(start,cursor_-start)));Need(decoded.is_string());
            auto value=decoded.get<crude_json::string>();Need(ValidScriptText(value,Scene::SceneDocument::MaxBytes));return value;
        }
        void Number(){Take('-');if(!Take('0')){Need(Peek()>='1' && Peek()<='9');while(Digit(Peek()))++cursor_;}
            if(Take('.')){Need(Digit(Peek()));while(Digit(Peek()))++cursor_;}
            if(Take('e') || Take('E')){if(!Take('+'))Take('-');Need(Digit(Peek()));while(Digit(Peek()))++cursor_;}}
        void Value(size_t depth){
            Need(depth<=64 && ++nodes_<=Scene::SceneDocument::MaxBytes);Space();
            if(Take('{')){std::set<std::string> keys;Space();if(Take('}'))return;
                do{Space();auto key=String();Need(keys.insert(std::move(key)).second);Space();Need(Take(':'));Value(depth+1);Space();if(Take('}'))return;Need(Take(','));}while(true);}
            if(Take('[')){Space();if(Take(']'))return;do{Value(depth+1);Space();if(Take(']'))return;Need(Take(','));}while(true);}
            if(Peek()=='"'){String();return;}
            for(const auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(text_.substr(cursor_,literal.size())==literal){cursor_+=literal.size();return;}
            Number();
        }
    public:
        explicit JsonGate(std::string_view text):text_(text){}
        void Validate(){Need(!text_.empty() && ValidScriptText(text_,Scene::SceneDocument::MaxBytes));Value(0);Space();Need(cursor_==text_.size());}
    };
    static V Json(std::string text){if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);JsonGate(text).Validate();auto value=V::parse(text);Need(value.is_object());return value;}
    using Resolved=Reference;
    static Resolved Resolve(const Assets::AssetRegistry& registry,const std::filesystem::path& root,std::string_view hint,std::string_view guid,Assets::AssetType type,ScriptPrefabWorkBudget* budget=nullptr){
        Need(ValidScriptText(hint,4096) && ValidScriptText(guid,32) && (!hint.empty() || !guid.empty()));
        Need(root.is_absolute());
        if(budget)Need(budget->Reference(),Result::Limit);Checks(budget);
        if(!registry.GetRoot().empty())Need(!std::filesystem::exists(registry.GetRoot()/".kubo/asset-move.pending.json"));
        std::filesystem::path source;
        if(!hint.empty()){
            const auto relative=Base::PathFromUtf8(std::string(hint));Need(!relative.has_root_path() && hint.find(':')==hint.npos);
            for(const auto& part:relative)Need(part!="..");source=(root/relative).lexically_normal();
        }
        const Assets::AssetRecord* record=nullptr;std::string identity(guid);
        if(!guid.empty()){
            Need(Scene::ModelComponent::IsValidGuid(identity));record=registry.FindByGuid(identity);Need(record!=nullptr,Result::Missing);
            Need(record->type==type);source=registry.ResolveGuid(identity).lexically_normal();
        }else{
            record=registry.FindByPath(source);Need(record!=nullptr,Result::Missing);Need(record->type==type);
            if(record->guid.size()==32 && Scene::ModelComponent::IsValidGuid(record->guid))identity=record->guid;
        }
        const auto relative=source.lexically_relative(root);Need(!relative.empty() && !relative.has_root_path());
        for(const auto& part:relative)Need(part!="..");
        auto cursor=root;Checks(budget);auto attributes=GetFileAttributesW(cursor.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)throw Refusal{MissingCode(GetLastError())};
        Need((attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT));
        for(const auto& part:relative){cursor/=part;Checks(budget);attributes=GetFileAttributesW(cursor.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)throw Refusal{MissingCode(GetLastError())};Need(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT));}
        File ordinary;Open(ordinary,source,GENERIC_READ,budget);
        if(!identity.empty()){
            const auto metadata=Json(Read(Assets::AssetRegistry::MetadataPath(source),16*1024,budget));
            Need(metadata.contains("version") && metadata["version"].is_number() && metadata["version"].get<crude_json::number>()==1 && metadata.contains("guid") && metadata["guid"].is_string() && metadata["guid"].get<crude_json::string>()==identity);
        }
        auto canonical=Base::PathToUtf8(relative);Need(ValidScriptText(canonical,4096,true));return {std::move(source),std::move(canonical),std::move(identity)};
    }
public:
    // Shared read-only primitives for export validation. These do not relax
    // Load's runtime requirement that every Script reference is a file asset.
    static Result ReadDefinition(const std::filesystem::path& path,V& output) noexcept {
        try{auto input=Json(Read(path,Scene::SceneDocument::MaxBytes));V candidate;std::string error;
            if(!Prefab::ValidatePrefabDocument(input,candidate,error))return Result::Invalid;
            output.swap(candidate);return Result::Success;
        }catch(const std::bad_alloc&){return Result::AllocationFailure;}
         catch(const Refusal& refusal){return refusal.result;}catch(...){return Result::Invalid;}
    }
    static Result ReferenceAsset(const Assets::AssetRegistry& registry,const std::filesystem::path& root,
        std::string_view hint,std::string_view guid,Assets::AssetType type,Reference& output,ScriptPrefabWorkBudget* budget=nullptr) noexcept {
        try{auto candidate=Resolve(registry,root,hint,guid,type,budget);output.source.swap(candidate.source);output.hint.swap(candidate.hint);output.guid.swap(candidate.guid);return Result::Success;
        }catch(const std::bad_alloc&){return Result::AllocationFailure;}
         catch(const Refusal& refusal){return refusal.result;}catch(...){return Result::Invalid;}
    }
    static Result Load(const Assets::AssetRegistry& registry,const std::filesystem::path& root,std::string_view hint,std::string_view guid,V& output,
        ScriptPrefabWorkBudget* budget=nullptr,size_t maximumEntities=10000) noexcept {
        try{
            Need(maximumEntities>0 && maximumEntities<=10000);
            const auto reference=Resolve(registry,root,hint,guid,Assets::AssetType::Prefab,budget);
            auto input=Json(Read(reference.source,Scene::SceneDocument::MaxBytes,budget));V candidate;std::string error;
            if(maximumEntities<10000 && input.contains("world") && input["world"].is_object() && input["world"].contains("entities") && input["world"]["entities"].is_array())
                Need(input["world"]["entities"].get<crude_json::array>().size()<=maximumEntities,Result::Limit);
            if(!Prefab::ValidatePrefabDocument(input,candidate,error))return Result::Invalid;
            Scene::EntityWorld world;if(!world.Restore(candidate["world"],error))return Result::Invalid;
            for(auto& entity:world.GetEntities()){
                if(entity.hasModel){auto ref=Resolve(registry,root,entity.model.assetPath,entity.model.assetGuid,Assets::AssetType::Model,budget);entity.model.assetPath.swap(ref.hint);entity.model.assetGuid.swap(ref.guid);}
                if(entity.hasScript){auto ref=Resolve(registry,root,entity.script.assetPath,entity.script.assetGuid,Assets::AssetType::Script,budget);entity.script.assetPath.swap(ref.hint);entity.script.assetGuid.swap(ref.guid);}
                if(entity.hasAudio && (!entity.audio.assetPath.empty() || !entity.audio.assetGuid.empty())){auto ref=Resolve(registry,root,entity.audio.assetPath,entity.audio.assetGuid,Assets::AssetType::Audio,budget);entity.audio.assetPath.swap(ref.hint);entity.audio.assetGuid.swap(ref.guid);}
                if(entity.hasSprite && (!entity.sprite.assetPath.empty() || !entity.sprite.assetGuid.empty())){auto ref=Resolve(registry,root,entity.sprite.assetPath,entity.sprite.assetGuid,Assets::AssetType::Texture,budget);entity.sprite.assetPath.swap(ref.hint);entity.sprite.assetGuid.swap(ref.guid);Need(entity.sprite.IsValid());}
                if(entity.hasParticle && (!entity.particle.assetPath.empty() || !entity.particle.assetGuid.empty())){auto ref=Resolve(registry,root,entity.particle.assetPath,entity.particle.assetGuid,Assets::AssetType::Texture,budget);entity.particle.assetPath.swap(ref.hint);entity.particle.assetGuid.swap(ref.guid);Need(entity.particle.IsValid());}
                if(entity.hasAnimator && !entity.animator.controllerGuid.empty()){Resolve(registry,root,"",entity.animator.controllerGuid,Assets::AssetType::AnimatorController,budget);}
            }
            candidate["world"]=world.Capture();Need(candidate.dump().size()<=Scene::SceneDocument::MaxBytes);
            output.swap(candidate);return Result::Success;
        }catch(const std::bad_alloc&){return Result::AllocationFailure;}
         catch(const Refusal& refusal){return refusal.result;}
         catch(...){return Result::Invalid;}
    }
};
}
