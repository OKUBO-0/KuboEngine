#pragma once
#include "ScriptProperty.h"
#include "../assets/ModelReference.h"
#include "../base/AssetPath.h"
#include "ScriptPrefabAdmission.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <new>

namespace Engine::Scripting {
// Host-owned, runtime-thread read-only admission. Registry must outlive this
// view and must not be rescanned concurrently. No import/metadata writes.
// Filesystem checks are fail-closed preflight, not a TOCTOU security sandbox.
class ScriptAssetView final {
public:
    enum class Result { Success,Missing,Invalid,AllocationFailure,Limit };
    ScriptAssetView(const Assets::AssetRegistry& registry,const std::filesystem::path& projectRoot)
        :registry_(registry),root_(projectRoot.lexically_normal()) {
        if(!root_.is_absolute())throw std::invalid_argument("Script asset owner must be an explicit absolute project root");
    }
    Result Prefab(std::string_view hint,std::string_view guid,Engine::Prefab::PrefabValue& output,
        ScriptPrefabWorkBudget* budget=nullptr,size_t maximumEntities=10000) const noexcept {
        const auto result=ScriptPrefabAdmission::Load(registry_,root_,hint,guid,output,budget,maximumEntities);
        switch(result){
        case ScriptPrefabAdmission::Result::Success:return Result::Success;
        case ScriptPrefabAdmission::Result::Missing:return Result::Missing;
        case ScriptPrefabAdmission::Result::AllocationFailure:return Result::AllocationFailure;
        case ScriptPrefabAdmission::Result::Limit:return Result::Limit;
        default:return Result::Invalid;
        }
    }
    Result Model(std::string_view hint,std::string_view guid,Scene::ModelComponent& output) const noexcept {
        if(!ValidScriptText(hint,4096) || !ValidScriptText(guid,32) || (hint.empty() && guid.empty()))return Result::Invalid;
        try {
            std::filesystem::path source;
            const Assets::AssetRecord* record=nullptr;
            if(!hint.empty()){
                const auto relative=Base::PathFromUtf8(std::string(hint));
                if(relative.has_root_path() || hint.find(':')!=hint.npos)return Result::Invalid;
                for(const auto& part:relative)if(part=="..")return Result::Invalid;
                source=(root_/relative).lexically_normal();
            }
            std::string identity(guid);
            if(!guid.empty()){
                if(!Scene::ModelComponent::IsValidGuid(identity))return Result::Invalid;
                record=registry_.FindByGuid(identity);
                if(!record)return Result::Missing;
                if(record->type!=Assets::AssetType::Model)return Result::Invalid;
                source=registry_.ResolveGuid(identity).lexically_normal();
            } else {
                record=registry_.FindByPath(source);
                if(!record)return Result::Missing;
                if(record->type!=Assets::AssetType::Model)return Result::Invalid;
                // Modern identities are pinned even when caller supplied a path.
                // ReadOnly legacy registries can have a 16-byte path hash only.
                if(record->guid.size()==32 && Scene::ModelComponent::IsValidGuid(record->guid))identity=record->guid;
            }
            const auto relative=source.lexically_relative(root_);
            if(relative.empty() || relative.has_root_path())return Result::Invalid;
            for(const auto& part:relative)if(part=="..")return Result::Invalid;
            auto cursor=root_;
            auto attributes=GetFileAttributesW(cursor.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES)return Result::Missing;
            if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return Result::Invalid;
            for(const auto& part:relative){
                cursor/=part;attributes=GetFileAttributesW(cursor.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES)return Result::Missing;
                if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return Result::Invalid;
            }
            const auto canonicalHint=Base::PathToUtf8(relative);
            if(!ValidScriptText(canonicalHint,4096,true))return Result::Invalid;
            std::string error;
            if(Assets::ResolveAssetReference(canonicalHint,identity,Assets::AssetType::Model,registry_,root_,error).empty())return Result::Invalid;
            Scene::ModelComponent candidate;candidate.assetPath=canonicalHint;candidate.assetGuid=std::move(identity);
            output.assetPath.swap(candidate.assetPath);output.assetGuid.swap(candidate.assetGuid);
            return Result::Success;
        } catch(const std::bad_alloc&) {return Result::AllocationFailure;}
          catch(...) {return Result::Invalid;}
    }
    Result Sprite(std::string_view hint,std::string_view guid,ScriptPrefabAdmission::Reference& output,ScriptPrefabWorkBudget* budget=nullptr) const noexcept {
        if(!Scene::UIComponentDetail::AssetPath(hint) || !Scene::UIComponentDetail::Guid(guid))return Result::Invalid;
        const auto result=ScriptPrefabAdmission::ReferenceAsset(registry_,root_,hint,guid,Assets::AssetType::Texture,output,budget);
        switch(result){
        case ScriptPrefabAdmission::Result::Success:return Result::Success;
        case ScriptPrefabAdmission::Result::Missing:return Result::Missing;
        case ScriptPrefabAdmission::Result::AllocationFailure:return Result::AllocationFailure;
        case ScriptPrefabAdmission::Result::Limit:return Result::Limit;
        default:return Result::Invalid;
        }
    }
    bool ButtonDestination(const std::string& guid) const {
        std::string error;
        return !Assets::ResolveAssetReference({},guid,Assets::AssetType::Scene,registry_,root_,error).empty();
    }
private:
    const Assets::AssetRegistry& registry_;
    std::filesystem::path root_;
};
}
