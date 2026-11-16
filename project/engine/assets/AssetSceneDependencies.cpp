#include "AssetSceneDependencies.h"
#include "SpriteReference.h"
#include "../base/BoundedJson.h"
#include "../base/ProjectContext.h"
#include <set>

namespace Engine::Assets {
namespace SceneDependencyDetail {
struct Reference {const char* label;std::string_view hint,guid;AssetType type;bool script=false,sprite=false,particle=false;};
inline std::string_view Text(const crude_json::value& value,const char* key){
    if(!value.contains(key))return {};
    if(!value[key].is_string())throw std::runtime_error("Scene asset reference must be a string");
    const auto& text=value[key].get<crude_json::string>();
    if(!Scripting::ValidScriptText(text,4096))throw std::runtime_error("Scene asset reference text is invalid/over 4096 bytes");return text;
}
template<class Consumer> void Visit(const crude_json::value& entity,SceneAssetReferenceScope scope,Consumer consume){
    if(entity.contains("sprite")){
        Scene::SpriteComponent sprite;if(!Scene::DecodeSprite(entity["sprite"],sprite))throw std::runtime_error("Sprite dependency component invalid");
        consume(Reference{"Sprite",sprite.assetPath,sprite.assetGuid,AssetType::Texture,false,true});
        if(scope==SceneAssetReferenceScope::All)consume(Reference{"UI Button",{},sprite.buttonSceneGuid,AssetType::Scene});
    }
    if(scope==SceneAssetReferenceScope::SpriteOnly)return;
    if(entity.contains("particle")){Scene::ParticleComponent value;if(!Scene::DecodeParticle(entity["particle"],value))throw std::runtime_error("Particle dependency component invalid");consume(Reference{"Particle",value.assetPath,value.assetGuid,AssetType::Texture,false,true,true});}
    consume(Reference{"Model",Text(entity,"model"),Text(entity,"modelGuid"),AssetType::Model});
    for(const auto* key:{"script","audio"})if(entity.contains(key)){
        const auto& value=entity[key];if(!value.is_object())throw std::runtime_error("Scene Script/Audio reference must be an object");
        consume(Reference{key,Text(value,"asset"),Text(value,"guid"),key[0]=='s'?AssetType::Script:AssetType::Audio,key[0]=='s'});
    }
    if(entity.contains("animator")){const auto& value=entity["animator"];if(!value.is_object())throw std::runtime_error("Scene Animator reference must be an object");consume(Reference{"Animator Controller",{},Text(value,"controllerGuid"),AssetType::AnimatorController});}
    if(entity.contains("prefabLink") && !entity["prefabLink"].is_null()){
        const auto& link=entity["prefabLink"];if(!link.is_object() || Text(link,"sourceGuid").empty())throw std::runtime_error("Scene Prefab link source reference invalid");
        // Only the live source link is a dependency. Its saved base/mapping is
        // history data, not another set of active Model/Script/Audio references.
        consume(Reference{"Prefab",{},Text(link,"sourceGuid"),AssetType::Prefab});
    }
}
template<class Consumer> void Visit(const Scene::EntityRecord& entity,SceneAssetReferenceScope scope,Consumer consume){
    if(entity.hasSprite){
        consume(Reference{"Sprite",entity.sprite.assetPath,entity.sprite.assetGuid,AssetType::Texture,false,true});
        if(scope==SceneAssetReferenceScope::All)consume(Reference{"UI Button",{},entity.sprite.buttonSceneGuid,AssetType::Scene});
    }
    if(scope==SceneAssetReferenceScope::SpriteOnly)return;
    if(entity.hasParticle)consume(Reference{"Particle",entity.particle.assetPath,entity.particle.assetGuid,AssetType::Texture,false,true,true});
    if(entity.hasModel)consume(Reference{"Model",entity.model.assetPath,entity.model.assetGuid,AssetType::Model});
    if(entity.hasScript)consume(Reference{"Script",entity.script.assetPath,entity.script.assetGuid,AssetType::Script,true});
    if(entity.hasAudio)consume(Reference{"Audio",entity.audio.assetPath,entity.audio.assetGuid,AssetType::Audio});
    if(entity.hasAnimator)consume(Reference{"Animator Controller",{},entity.animator.controllerGuid,AssetType::AnimatorController});
    if(!entity.prefabLink.is_null()){if(!entity.prefabLink.is_object() || Text(entity.prefabLink,"sourceGuid").empty())throw std::runtime_error("Open Scene Prefab source reference invalid");consume(Reference{"Prefab",{},Text(entity.prefabLink,"sourceGuid"),AssetType::Prefab});}
}
inline bool ScriptFileSyntax(const std::string_view text,const std::filesystem::path& path){
    if(text.find(':')!=text.npos || path.has_root_name() || path.has_root_directory())return false;
    for(const auto& part:path)if(part=="..")return false;return true;
}
inline bool Resolve(const Reference& ref,const AssetRegistry& registry,const std::filesystem::path& root,
    std::filesystem::path& source,const AssetRecord*& target){
    if(ref.hint.empty() && ref.guid.empty())return false;
    if(!ref.guid.empty()){
        std::string error;
        if(ref.sprite){Scene::SpriteComponent value;value.assetPath=ref.hint;value.assetGuid=ref.guid;source=ResolveSpriteReference(value,registry,root,error);}
        else source=ResolveAssetReference(std::string(ref.hint),std::string(ref.guid),ref.type,registry,root,error);
        if(source.empty())throw std::runtime_error(std::string(ref.label)+" GUID refused: "+error);target=registry.FindByGuid(std::string(ref.guid));return true;
    }
    const auto path=Base::PathFromUtf8(std::string(ref.hint));if(ref.script && !ScriptFileSyntax(ref.hint,path))return false;
    source=(path.is_absolute()?path:root/path).lexically_normal();target=registry.FindByPath(source);
    if(ref.script && !target && AssetRegistry::Classify(path)!=AssetType::Script && ref.hint.find_first_of("/\\")==ref.hint.npos)return false; // Opaque registered native callback key.
    if(ref.sprite && target && target->type!=AssetType::Texture)throw std::runtime_error("Sprite dependency path is not a Texture");
    // Path-only Model/Script/Audio uses existing legacy file semantics. Typed
    // GUID references above remain strict; do not reject a legacy file solely
    // because its extension is not classified as that component's asset type.
    return true;
}
inline bool Same(const std::filesystem::path& a,const std::filesystem::path& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
inline bool Inside(const std::filesystem::path& path,const std::filesystem::path& root){
    auto part=path.begin();for(const auto& component:root){if(part==path.end() || !Same(component,*part))return false;++part;}return true;
}
inline bool Reserved(const std::filesystem::path& name){
    for(const auto* value:{L".git",L".vs",L".kubo",L"Library",L"Build",L"Builds",L"generated",L"UserSettings",L"ProjectSettings"})if(Same(name,value))return true;return false;
}
inline void Ordinary(const std::filesystem::path& path,const std::filesystem::path& root){
    if(!Inside(path,root))throw std::runtime_error("Scene asset dependency file escaped project root");
    auto current=root;const auto inspect=[](const auto& p){const auto attributes=GetFileAttributesW(p.c_str());if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Scene asset dependency path is missing or linked");};
    inspect(current);for(const auto& component:path.lexically_relative(root)){current/=component;inspect(current);}
}
inline std::string Read(const std::filesystem::path& path,size_t& total,const std::function<void()>& check){
    struct File{HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} file;
    file.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(file.handle==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || !GetFileSizeEx(file.handle,&size) || size.QuadPart<0 || size.QuadPart>4*1024*1024)
        throw std::runtime_error("Scene asset dependency source unreadable/linked/over 4 MiB");
    if(static_cast<size_t>(size.QuadPart)>32*1024*1024-total)throw std::runtime_error("Scene asset dependency documents exceed 32 MiB aggregate budget");
    total+=static_cast<size_t>(size.QuadPart);std::string bytes(static_cast<size_t>(size.QuadPart),'\0');
    for(size_t offset=0;offset<bytes.size();){check();DWORD count=0;const auto chunk=static_cast<DWORD>((std::min)(size_t(65536),bytes.size()-offset));if(!ReadFile(file.handle,bytes.data()+offset,chunk,&count,nullptr) || count!=chunk)throw std::runtime_error("Scene asset dependency source read failed");offset+=count;}
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.erase(0,3);std::string error;check();
    if(!Base::BoundedJson::Validate(bytes,{4*1024*1024,64,1024*1024},error))throw std::runtime_error("Scene asset dependency JSON: "+error);
    check();return bytes;
}
}
// Extends a worker-owned graph. Project paths are rooted at the explicit owner,
// not at the Scene/Prefab's folder. GUID edges remain movable; path edges do not.
// This is bounded read-only admission, not full World validation or a lease over
// every document until the later file move. External writers can change files.
void AppendProjectAssetDependencies(AssetDependencies& graph,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile,const std::function<bool()>& cancelled,SceneAssetReferenceScope scope){
    const auto check=[&]{if(cancelled && cancelled())throw std::runtime_error("Scene asset dependency inspection cancelled");};
    size_t bytes=0,entities=0,entries=0,references=0;
    try{
        check();if(scope!=SceneAssetReferenceScope::All && scope!=SceneAssetReferenceScope::SpriteOnly)throw std::runtime_error("Unknown Scene reference scope");if(projectRoot.empty() || !projectRoot.is_absolute() || (!projectFile.empty() && !projectFile.is_absolute()))throw std::runtime_error("Scene asset dependency inspection requires explicit absolute project paths");
        const auto root=std::filesystem::canonical(projectRoot);SceneDependencyDetail::Ordinary(root,root);
        const auto less=[](const std::filesystem::path& a,const std::filesystem::path& b){const auto order=CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE);if(!order)throw std::runtime_error("Scene asset dependency path comparison failed");return order==CSTR_LESS_THAN;};
        std::set<std::filesystem::path,decltype(less)> documents(less);
        const auto add=[&](const auto& path){check();if(documents.size()>=4096 && !documents.contains(path))throw std::runtime_error("Scene asset dependency document count exceeds 4096");documents.insert(path.lexically_normal());};
        for(const auto& asset:registry.GetAssets())if(asset.type==AssetType::Scene || asset.type==AssetType::Prefab)add(Base::PathFromUtf8(asset.absolutePath));
        if(!projectFile.empty()){
            Base::ProjectContext project;std::string error;if(!project.Open(projectFile,error))throw std::runtime_error(error);
            if(!SceneDependencyDetail::Same(project.GetRootPath(),root))throw std::runtime_error("Scene asset dependency manifest belongs to another project");
            add(project.ResolvePath(project.GetSettings().startupScene));
        }
        // Registered JSON Scenes plus the explicitly selected startup JSON are
        // inspected. Other closed project documents use .scene/.prefab suffixes.
        for(auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it){
            check();if(++entries>65536)throw std::runtime_error("Scene asset dependency inventory exceeds 65536 entries");
            const auto name=it->path().filename(),parent=it->path().parent_path();
            const bool control=SceneDependencyDetail::Same(name,L".git") || SceneDependencyDetail::Same(name,L".vs") || SceneDependencyDetail::Same(name,L".kubo");
            if(control || ((SceneDependencyDetail::Same(parent,root) || SceneDependencyDetail::Same(parent,registry.GetRoot())) && SceneDependencyDetail::Reserved(name))){it.disable_recursion_pending();continue;}
            const auto attributes=GetFileAttributesW(it->path().c_str());if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked/unreadable project entry prevents Scene asset move inspection");
            if(it->is_regular_file() && (SceneDependencyDetail::Same(it->path().extension(),L".scene") || SceneDependencyDetail::Same(it->path().extension(),L".prefab")))add(it->path());
        }
        for(const auto& path:documents){
            check();const auto* owner=registry.FindByPath(path);const std::string ownerKey=owner?owner->guid:"project:"+Base::PathToUtf8(path.lexically_relative(root));
            try{
                SceneDependencyDetail::Ordinary(path,root);const auto text=SceneDependencyDetail::Read(path,bytes,check);const auto json=crude_json::value::parse(text);
                if(!json.is_object())throw std::runtime_error("Scene asset dependency document must be an object");
                const crude_json::value* world=&json;
                if(SceneDependencyDetail::Same(path.extension(),L".prefab")){
                    if(!json.contains("format") || !json["format"].is_string() || json["format"].get<crude_json::string>()!="kubo-prefab-1" || !json.contains("world") || !json["world"].is_object())throw std::runtime_error("Scene asset dependency Prefab envelope invalid");world=&json["world"];
                }
                if(!world->contains("entities"))continue; // Legacy non-Entity Scene.
                if(!(*world)["entities"].is_array() || (*world)["entities"].get<crude_json::array>().size()>10000)throw std::runtime_error("Scene asset dependency entity collection invalid/over 10000");
                std::vector<AssetDependency> next;
                const auto edgeLess=[&](const auto& a,const auto& b){if(less(a.first,b.first))return true;if(less(b.first,a.first))return false;return a.second<b.second;};
                std::set<std::pair<std::filesystem::path,AssetReferenceKind>,decltype(edgeLess)> seen(edgeLess);
                for(const auto& entity:(*world)["entities"].get<crude_json::array>()){
                    check();if(++entities>65536)throw std::runtime_error("Scene asset dependency aggregate entity work exceeds 65536");
                    if(!entity.is_object())throw std::runtime_error("Scene asset dependency entity invalid");
                    SceneDependencyDetail::Visit(entity,scope,[&](const SceneDependencyDetail::Reference& ref){
                        check();if(ref.hint.empty() && ref.guid.empty())return;
                        if(++references>65536)throw std::runtime_error("Scene asset reference work exceeds 65536");
                        const AssetRecord* target=nullptr;std::filesystem::path source;if(!SceneDependencyDetail::Resolve(ref,registry,root,source,target))return;
                        const auto kind=ref.guid.empty()?AssetReferenceKind::ProjectPath:AssetReferenceKind::Guid;
                        const auto relative=Base::PathToUtf8(source.lexically_relative(registry.GetRoot()));
                        if(!seen.emplace(source,kind).second)return;
                        if(graph.edges.size()+next.size()>=65536)throw std::runtime_error("Scene asset dependency edge limit exceeded");
                        next.push_back({ownerKey,target?target->guid:std::string{},std::string(ref.hint),relative,target==nullptr,kind});
                    });
                }
                graph.edges.reserve(graph.edges.size()+next.size());for(auto& edge:next)graph.edges.push_back(std::move(edge));
            }catch(const std::exception& error){check();graph.issues.push_back({ownerKey,Base::PathToUtf8(path.lexically_relative(root))+": "+error.what()});}
        }
        check();
    }catch(const std::exception& error){check();graph.issues.push_back({"project-asset-inventory",error.what()});}
}
std::string OpenWorldAssetMoveRisk(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::string& guid,SceneAssetReferenceScope scope){
    if(scope!=SceneAssetReferenceScope::All && scope!=SceneAssetReferenceScope::SpriteOnly)return "Unknown Scene reference scope";
    const auto* selected=registry.FindByGuid(guid);if(!selected || (scope==SceneAssetReferenceScope::SpriteOnly && selected->type!=AssetType::Texture))return {};
    const auto source=std::filesystem::weakly_canonical(Base::PathFromUtf8(selected->absolutePath));
    for(const auto& entity:world.GetEntities()){
        std::string risk;
        SceneDependencyDetail::Visit(entity,scope,[&](const SceneDependencyDetail::Reference& ref){
            if(!risk.empty() || !ref.guid.empty() || ref.hint.empty())return;
            if(!Scripting::ValidScriptText(ref.hint,4096))throw std::runtime_error("Invalid open Scene asset reference text");
            if(ref.sprite && !(ref.particle?entity.particle.IsValid():entity.sprite.IsValid()))throw std::runtime_error("Open Scene has an invalid Particle/Sprite reference");
            const auto path=Base::PathFromUtf8(std::string(ref.hint));if(ref.script && !SceneDependencyDetail::ScriptFileSyntax(ref.hint,path))return;
            const auto target=std::filesystem::weakly_canonical(path.is_absolute()?path:projectRoot/path);
            if(SceneDependencyDetail::Same(source,target))risk=std::string("Open Scene ")+ref.label+" uses this asset path; select a registered GUID reference or rewrite it before moving";
        });
        if(!risk.empty())return risk;
    }
    return {};
}
}
