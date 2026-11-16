#include "McpCommandBridge.h"
#include "McpEntityArguments.h"
#include "McpComponentArguments.h"
#include "../assets/ModelReference.h"

#include <stdexcept>
#include <cstring>
#include <type_traits>
#include <exception>
#include <Windows.h>

namespace Engine::Services {
namespace {
static_assert(std::is_nothrow_copy_assignable_v<Scene::CameraComponent>);
static_assert(std::is_nothrow_copy_assignable_v<Physics::ColliderComponent>);
static_assert(std::is_nothrow_copy_assignable_v<Physics::RigidBodyComponent>);
static_assert(std::is_nothrow_move_assignable_v<Scene::ModelComponent>);
static_assert(std::is_nothrow_move_assignable_v<Scripting::ScriptComponent>);
static_assert(std::is_nothrow_move_assignable_v<AudioSystem::AudioComponent>);
static_assert(std::is_nothrow_copy_assignable_v<Scene::CanvasComponent>);
static_assert(std::is_nothrow_copy_assignable_v<Scene::LightComponent> && std::is_nothrow_copy_assignable_v<Scene::MaterialComponent>);
static_assert(std::is_nothrow_move_assignable_v<Scene::SpriteComponent>);

bool Within(const std::filesystem::path& path,const std::filesystem::path& root) {
    auto part=path.begin();
    for(const auto& component:root) {
        if(part==path.end() || CompareStringOrdinal(component.c_str(),-1,part->c_str(),-1,TRUE)!=CSTR_EQUAL)return false;
        ++part;
    }
    return true;
}
bool ValidateAsset(const std::string& hint,const std::string& guid,Assets::AssetType type,
    const Assets::AssetRegistry* registry,const std::filesystem::path& projectRoot,std::string& error) {
    try {
        if(!ValidMcpAssetPath(hint))throw std::runtime_error("Component asset path must be bounded UTF-8 and project-relative");
        if(hint.empty() && guid.empty())return true; // Unassigned Audio / removed Model or Script.
        if(!registry || registry->GetRoot().empty())throw std::runtime_error("Asset Component editing requires the active project registry");
        const auto root=std::filesystem::canonical(projectRoot),assets=std::filesystem::canonical(registry->GetRoot());
        if(!Within(assets,root))throw std::runtime_error("Asset registry belongs outside the active project");
        if(std::filesystem::exists(assets/".kubo/asset-move.pending.json"))throw std::runtime_error("Pending asset move requires recovery before Component editing");
        if(!hint.empty() && !Within(std::filesystem::weakly_canonical(root/std::filesystem::path(std::u8string(hint.begin(),hint.end()))),assets))
            throw std::runtime_error("Component asset hint escapes the project's asset directory");
        const auto source=Assets::ResolveAssetReference(hint,guid,type,*registry,root,error);
        if(source.empty())return false;
        if(!Within(std::filesystem::canonical(source),assets) || Assets::AssetRegistry::Classify(source)!=type)
            throw std::runtime_error("Component asset source is outside the asset directory or has the wrong type");
        auto current=std::filesystem::absolute(source).root_path();
        for(const auto& part:std::filesystem::absolute(source).relative_path()) {
            current/=part;const auto attributes=GetFileAttributesW(current.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked or unavailable Component asset paths are not supported");
        }
        return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

Engine::Scene::SceneValue Error(const Engine::Scene::SceneValue& id, const char* message, int code = -32600)
{
    Engine::Scene::SceneValue response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["error"]["code"] = double(code);
    response["error"]["message"] = message;
    return response;
}

} // namespace

McpCommandBridge::McpCommandBridge(std::filesystem::path projectRoot,const Assets::AssetRegistry* assets)
    : projectRoot_(std::move(projectRoot)),assets_(assets)
{
}

Engine::Scene::SceneValue McpCommandBridge::Handle(const Engine::Scene::SceneValue& request,
    Engine::Scene::EntityWorld& world) const
{
    const auto id = request.contains("id") ? request["id"] : Engine::Scene::SceneValue(nullptr);
    if (!request.is_object() || !request.contains("method") || !request["method"].is_string()) return Error(id, "Invalid request");
    const auto method = request["method"].get<crude_json::string>();
    Engine::Scene::SceneValue response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    try {
        if (method == "scene.list_entities") {
            response["result"] = world.Capture()["entities"];
            return response;
        }
        if (method == "scene.create_entity") {
            std::string name = "Entity";
            if (request.contains("params") && (!request["params"].is_object() ||
                (request["params"].contains("name") && !request["params"]["name"].is_string()))) return Error(id, "scene.create_entity requires a string name", -32602);
            if (request.contains("params") && request["params"].is_object() && request["params"].contains("name") && request["params"]["name"].is_string()) {
                name = request["params"]["name"].get<crude_json::string>();
            }
            Scene::EntityWorld checked;std::string error;
            if(!checked.Restore(world.Capture(),error))return Error(id,error.c_str(),-32000);
            auto candidate=world;
            const auto entity = candidate.CreateEntity(name);
            if(!entity)return Error(id,"Entity creation exhausted JSON-exact IDs or invalid allocation data",-32000);
            if(candidate.Capture().dump().size()>Scene::SceneDocument::MaxBytes)return Error(id,"Entity creation exceeds Scene document budget",-32000);
            world=std::move(candidate);
            response["result"]["entityId"] = double(entity);
            return response;
        }
        if(method=="scene.set_component") {
            std::string error;
            if(!request.contains("params") || !ValidateMcpComponentArguments(request["params"],error))
                return Error(id,error.empty()?"Component tool requires params":error.c_str(),-32602);
            const auto& arguments=request["params"];
            const auto entityId=static_cast<Scene::EntityId>(arguments["entityId"].get<double>());
            auto* entity=world.Find(entityId);
            if(!entity)return Error(id,"Entity does not exist",-32000);
            const auto& component=arguments["component"].get<std::string>();
            auto before=world.Capture();auto document=before;
            for(auto& value:document["entities"].get<crude_json::array>())if(value["id"].get<double>()==double(entityId)) {
                if(component=="model") {
                    if(arguments["patch"].is_null()){value["model"]="";value.get<crude_json::object>().erase("modelGuid");}
                    else for(const auto& [field,item]:arguments["patch"].get<crude_json::object>()) {
                        if(field=="asset")value["model"]=item;
                        else if(item.is_null())value.get<crude_json::object>().erase("modelGuid");
                        else value["modelGuid"]=item;
                    }
                } else if(arguments["patch"].is_null())value.get<crude_json::object>().erase(component);
                else for(const auto& [field,item]:arguments["patch"].get<crude_json::object>()) {
                    if(field=="guid" && item.is_null()){if(value.contains(component) && value[component].is_object())value[component].get<crude_json::object>().erase("guid");}
                    else value[component][field]=item;
                }
                break;
            }
            Scene::EntityWorld checked;
            if(!checked.Restore(document,error))return Error(id,error.c_str(),-32000);
            if(checked.Capture().dump().size()>Scene::SceneDocument::MaxBytes)return Error(id,"Component patch exceeds Scene document budget",-32000);
            // Copy only the requested component from the validated authored candidate.
            // Do not reset other Entities' script lifecycle, grounded state or the ID allocator.
            const auto& validated=*checked.Find(entityId);const bool removed=arguments["patch"].is_null();
            if(component=="model" && !ValidateAsset(validated.model.assetPath,validated.model.assetGuid,Assets::AssetType::Model,assets_,projectRoot_,error))return Error(id,error.c_str(),-32000);
            if(component=="script" && !ValidateAsset(validated.script.assetPath,validated.script.assetGuid,Assets::AssetType::Script,assets_,projectRoot_,error))return Error(id,error.c_str(),-32000);
            if(component=="audio" && !ValidateAsset(validated.audio.assetPath,validated.audio.assetGuid,Assets::AssetType::Audio,assets_,projectRoot_,error))return Error(id,error.c_str(),-32000);
            if(component=="sprite" && !ValidateAsset(validated.sprite.assetPath,validated.sprite.assetGuid,Assets::AssetType::Texture,assets_,projectRoot_,error))return Error(id,error.c_str(),-32000);
            if(component=="sprite" && !ValidateAsset({},validated.sprite.buttonSceneGuid,Assets::AssetType::Scene,assets_,projectRoot_,error))return Error(id,error.c_str(),-32000);
            const auto apply=[&](Scene::EntityRecord& target) {
                if(component=="camera"){target.hasCamera=validated.hasCamera;target.camera=validated.camera;}
                else if(component=="collider"){target.hasCollider=validated.hasCollider;target.collider=validated.collider;}
                else if(component=="rigidBody"){const auto grounded=target.rigidBody.grounded;target.hasRigidBody=validated.hasRigidBody;target.rigidBody=validated.rigidBody;target.rigidBody.grounded=removed?false:grounded;}
                else if(component=="model"){target.hasModel=validated.hasModel;target.model=validated.model;}
                else if(component=="script"){target.hasScript=validated.hasScript;target.script=validated.script;target.script.started=false;}
                else if(component=="light"){target.hasLight=validated.hasLight;target.light=validated.light;}
                else if(component=="material"){target.hasMaterial=validated.hasMaterial;target.material=validated.material;}
                else if(component=="canvas"){target.hasCanvas=validated.hasCanvas;target.canvas=validated.canvas;}
                else if(component=="sprite"){target.hasSprite=validated.hasSprite;target.sprite=validated.sprite;}
                else {target.hasAudio=validated.hasAudio;target.audio=validated.audio;}
            };
            auto after=world;apply(*after.Find(entityId));
            response["result"]["entityId"]=double(entityId);response["result"]["component"]=component;
            const bool changed=before.dump()!=after.Capture().dump();response["result"]["updated"]=changed;
            if(!changed)return response;
            // All string/property allocations are private above. Move only the selected
            // component, never EntityRecord's potentially throwing Prefab JSON assignment.
            auto* prepared=after.Find(entityId);
            const auto publish=[&]() noexcept {
                if(component=="camera"){entity->hasCamera=prepared->hasCamera;entity->camera=prepared->camera;}
                else if(component=="collider"){entity->hasCollider=prepared->hasCollider;entity->collider=prepared->collider;}
                else if(component=="rigidBody"){entity->hasRigidBody=prepared->hasRigidBody;entity->rigidBody=prepared->rigidBody;}
                else if(component=="model"){entity->hasModel=prepared->hasModel;entity->model=std::move(prepared->model);}
                else if(component=="script"){entity->hasScript=prepared->hasScript;entity->script=std::move(prepared->script);}
                else if(component=="light"){entity->hasLight=prepared->hasLight;entity->light=prepared->light;}
                else if(component=="material"){entity->hasMaterial=prepared->hasMaterial;entity->material=prepared->material;}
                else if(component=="canvas"){entity->hasCanvas=prepared->hasCanvas;entity->canvas=prepared->canvas;}
                else if(component=="sprite"){entity->hasSprite=prepared->hasSprite;entity->sprite=std::move(prepared->sprite);}
                else {entity->hasAudio=prepared->hasAudio;entity->audio=std::move(prepared->audio);}
            };
            // MSVC's JSON object move can allocate a map sentinel even when
            // returning a fully prepared response. Commit only after the return
            // object is constructed; exception unwinding must not publish.
            struct ReturnPublication {
                const decltype(publish)& action;
                int exceptions=std::uncaught_exceptions();
                ~ReturnPublication() noexcept {if(std::uncaught_exceptions()==exceptions)action();}
            } publication{publish};
            return response;
        }
        if(method=="scene.set_parent") {
            std::string error;
            if(!request.contains("params") || !ValidateMcpParentArguments(request["params"],error))
                return Error(id,error.empty()?"Parent tool requires params":error.c_str(),-32602);
            const auto entityId=static_cast<Scene::EntityId>(request["params"]["entityId"].get<double>());
            const auto parentId=static_cast<Scene::EntityId>(request["params"]["parentId"].get<double>());
            const auto* entity=world.Find(entityId);
            if(!entity) return Error(id,"Entity does not exist",-32000);
            const bool changed=entity->parent!=parentId;
            const bool keepWorld=request["params"].contains("keepWorld") && request["params"]["keepWorld"].get<bool>();
            if(!world.SetParent(entityId,parentId,keepWorld,error)) return Error(id,error.c_str(),-32000);
            response["result"]["entityId"]=double(entityId);response["result"]["parentId"]=double(parentId);response["result"]["updated"]=changed;
            return response;
        }
        if(method=="scene.set_transform" || method=="scene.delete_entity") {
            const bool transform=method=="scene.set_transform";
            std::string error;
            if(!request.contains("params") || !ValidateMcpEntityArguments(request["params"],transform,error))
                return Error(id,error.empty()?"Entity tool requires params":error.c_str(),-32602);
            const auto& arguments=request["params"];
            const auto entityId=static_cast<Scene::EntityId>(arguments["entityId"].get<double>());
            auto* entity=world.Find(entityId);
            if(!entity) return Error(id,"Entity does not exist",-32000);
            response["result"]["entityId"]=double(entityId);
            if(transform) {
                auto candidate=entity->transform;
                const auto read=[&](const char* field,Vector3& vector) {
                    if(arguments.contains(field)) {
                        const auto& array=arguments[field];
                        vector={static_cast<float>(array[0].get<double>()),static_cast<float>(array[1].get<double>()),static_cast<float>(array[2].get<double>())};
                    }
                };
                read("position",candidate.position);read("rotation",candidate.rotation);read("scale",candidate.scale);
                response["result"]["updated"]=true;
                entity->transform=candidate;
            } else {
                response["result"]["deleted"]=true;
                world.DestroyEntity(entityId); // Existing semantics: direct children become roots, local TRS retained.
            }
            return response;
        }
        if (method == "scene.save") {
            if (!request.contains("params") || !request["params"].is_object() || !request["params"].contains("path") || !request["params"]["path"].is_string()) return Error(id, "scene.save requires params.path", -32602);
            const auto text = request["params"]["path"].get<crude_json::string>();
            if (text.empty() || text.find('\0') != std::string::npos || text.find(':') != std::string::npos) return Error(id, "scene.save requires a project-relative scene path", -32602);
            std::u8string utf8(text.size(), u8'\0');
            std::memcpy(utf8.data(), text.data(), text.size());
            const auto relative = std::filesystem::path(utf8).lexically_normal();
            if (relative.has_root_path()) return Error(id, "scene.save path must be project-relative", -32602);
            for (const auto& component : relative) if (component == L"..") return Error(id, "scene.save path escapes the project", -32602);
            if (relative.extension() != L".scene" && relative.extension() != L".json") return Error(id, "scene.save requires .scene or .json", -32602);
            const auto root = std::filesystem::canonical(projectRoot_);
            const auto target = std::filesystem::weakly_canonical(root / relative);
            auto part = target.begin();
            for (const auto& component : root) {
                if (part == target.end() || CompareStringOrdinal(component.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL)
                    return Error(id, "scene.save resolves outside the project", -32602);
                ++part;
            }
            std::string error;
            if (!world.Save(target, error)) return Error(id, error.c_str(), -32000);
            response["result"]["saved"] = true;
            return response;
        }
        return Error(id, "Method not found", -32601);
    } catch (const std::exception& exception) {
        return Error(id, exception.what(), -32000);
    }
}

} // namespace Engine::Services
