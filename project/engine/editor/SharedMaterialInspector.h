#pragma once

#include "../assets/MaterialAsset.h"
#include "../base/AssetPath.h"
#include "RenderComponentInspector.h"
#include <array>

namespace Engine::Editor {

class SharedMaterialInspector final {
public:
    void BeginFrame() noexcept {preview_=false;}
    const Scene::MaterialValues* Preview() const noexcept {return preview_?&draft_:nullptr;}
    bool Draw(Scene::MaterialComponent& value,Assets::AssetRegistry& registry,std::string& status,bool editable) {
        if(registry.GetRoot().empty())return false;
        const auto* selected=registry.FindByGuid(value.assetGuid);
        const char* label=value.assetGuid.empty()?"Local Values":
            (selected && selected->type==Assets::AssetType::Material?selected->relativePath.c_str():"Missing Shared Material");
        ImGui::BeginDisabled(!editable);
        bool changed=false;
        if(ImGui::BeginCombo("Material Source",label)){
            if(ImGui::Selectable("Local Values",value.assetGuid.empty()) && !value.assetGuid.empty()){
                value.assetGuid.clear();changed=true;
            }
            for(const auto& asset:registry.GetAssets()){
                if(asset.type!=Assets::AssetType::Material)continue;
                ImGui::PushID(asset.guid.c_str());
                const bool assign=ImGui::Selectable(asset.relativePath.c_str(),value.assetGuid==asset.guid);
                ImGui::PopID();
                if(!assign)continue;
                std::string error;
                Scene::MaterialComponent material;
                const auto path=Assets::ResolveMaterialAsset(asset.guid,registry,error);
                if(!path.empty() && Assets::ReadMaterialAsset(path,material,error)){
                    value.assetGuid=asset.guid;changed=true;
                }else status="Cannot assign Material: "+error;
            }
            ImGui::EndCombo();
        }
        if(RenderInspectorButton("Create Shared Material...")){
            filename_.fill(0);
            ImGui::OpenPopup("Create Shared Material");
        }
        if(!value.assetGuid.empty()){
            ImGui::SameLine();
            if(RenderInspectorButton("Edit Shared Material...")){
                draftGuid_=value.assetGuid;
                if(Load(registry,status))ImGui::OpenPopup("Edit Shared Material");
            }
        }
        if(ImGui::BeginPopup("Create Shared Material")){
            ImGui::InputText("Name",filename_.data(),filename_.size());
            if(RenderInspectorButton("Create and Assign") && Create(value,registry,status)){
                changed=true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(ImGui::BeginPopup("Edit Shared Material")){
            if(draftGuid_!=value.assetGuid || !editable)ImGui::CloseCurrentPopup();
            else {
                DrawMaterialValues(draft_);
                preview_=draft_.IsValid();
                ImGui::TextWrapped("Preview on the selected Entity in Scene. Saving updates every Entity using this material.");
                if(RenderInspectorButton("Save Shared Material") && Save(registry,status)){
                    preview_=false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if(RenderInspectorButton("Reload"))Load(registry,status);
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        return changed;
    }
private:
    bool Create(Scene::MaterialComponent& value,Assets::AssetRegistry& registry,std::string& status) {
        try {
            const std::string name=filename_.data();
            if(name.empty() || name.find_first_of("<>:\"/\\|?*")!=name.npos || name.back()=='.' ||
                name.back()==' ' || name.front()=='.' ||
                name.find_first_of("\t\n\r")!=name.npos)
                throw std::runtime_error("Enter a material name using a single filename");
            const auto& root=registry.GetRoot();
            if(std::filesystem::exists(root/".kubo/asset-move.pending.json"))
                throw std::runtime_error("Resolve the pending asset move first");
            const auto path=root/Base::PathFromUtf8(name+".material");
            if(std::filesystem::exists(Assets::AssetRegistry::MetadataPath(path)))
                throw std::runtime_error("A Material identity already exists for this name");
            auto settings=value;
            std::string error;
            if(!value.assetGuid.empty()){
                const auto source=Assets::ResolveMaterialAsset(value.assetGuid,registry,error);
                if(source.empty() || !Assets::ReadMaterialAsset(source,settings,error))throw std::runtime_error(error);
            }
            if(!Assets::WriteMaterialAsset(path,settings,error,false))throw std::runtime_error(error);
            if(!registry.RegisterSource(path,value.assetGuid,error)){
                status="Material file saved but registration failed: "+error;
                return false;
            }
            status="Created shared Material "+name;
            return true;
        }catch(const std::exception& exception){status="Cannot create Material: "+std::string(exception.what());return false;}
    }

    bool Load(const Assets::AssetRegistry& registry,std::string& status) {
        std::string error;
        const auto path=Assets::ResolveMaterialAsset(draftGuid_,registry,error);
        if(!path.empty() && Assets::ReadMaterialAsset(path,draft_,error,&draftRevision_)){
            draftSource_=path;
            return true;
        }
        status="Cannot load Material: "+error;
        return false;
    }

    bool Save(const Assets::AssetRegistry& registry,std::string& status) {
        Scene::MaterialComponent current;
        std::string error,revision;
        const auto path=Assets::ResolveMaterialAsset(draftGuid_,registry,error);
        if(path.empty() || !Assets::ReadMaterialAsset(path,current,error,&revision)){
            status="Cannot save Material: "+error;
            return false;
        }
        if(path!=draftSource_ || revision!=draftRevision_){
            status="Material changed on disk. Reload before saving.";
            return false;
        }
        if(!Assets::WriteMaterialAsset(path,draft_,error,true)){
            status="Cannot save Material: "+error;
            return false;
        }
        status="Saved shared Material";
        return true;
    }

    bool preview_=false;
    std::array<char,192> filename_{};
    Scene::MaterialComponent draft_;
    std::filesystem::path draftSource_;
    std::string draftGuid_,draftRevision_;
};

} // namespace Engine::Editor
