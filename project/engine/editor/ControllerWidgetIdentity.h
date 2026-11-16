#pragma once
#include <imgui.h>
#include <cstdint>
namespace Engine::Editor::ControllerDetails {
struct IdentityScope {
    explicit IdentityScope(uint64_t id){ImGui::PushID(static_cast<int>(id>>32));ImGui::PushID(static_cast<int>(id&0xffffffff));}
    ~IdentityScope(){ImGui::PopID();ImGui::PopID();}
    IdentityScope(const IdentityScope&)=delete;IdentityScope& operator=(const IdentityScope&)=delete;
};
struct OwnerScope {
    explicit OwnerScope(const void* owner,uint64_t epoch){ImGui::PushID(owner);ImGui::PushID(static_cast<int>(epoch>>32));ImGui::PushID(static_cast<int>(epoch&0xffffffff));}
    ~OwnerScope(){ImGui::PopID();ImGui::PopID();ImGui::PopID();}
    OwnerScope(const OwnerScope&)=delete;OwnerScope& operator=(const OwnerScope&)=delete;
};
}
