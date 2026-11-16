#pragma once
#include <imgui.h>
#include <imgui_internal.h>

namespace Engine::Editor::DocumentShortcuts {
enum class Command { None, Undo, Redo, Duplicate, Delete };

// Named root plus ordinary child windows only: never a sibling dock tab, popup
// or a merely visible document. Callable before that root's Begin this frame.
inline Command Take(const char* rootName,bool enabled,bool allowDuplicate=false,bool allowDelete=false) {
    if(!enabled || !ImGui::GetCurrentContext())return Command::None;
    auto& io=ImGui::GetIO();auto& context=*ImGui::GetCurrentContext();
    if(io.KeyAlt || io.KeySuper || io.WantTextInput || ImGui::IsAnyItemActive() ||
       context.DragDropActive || ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2) ||
       ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))return Command::None;
    auto* root=ImGui::FindWindowByName(rootName);auto* focused=context.NavWindow;
    if(!root || !focused || (!root->Active && !root->WasActive) || root->Hidden || root->Collapsed ||
       !ImGui::IsWindowChildOf(focused,root,false,false))return Command::None;
    ImGuiKey key=ImGuiKey_None;Command command=Command::None;
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z,ImGuiInputFlags_None,ImGuiKeyOwner_NoOwner)){key=ImGuiKey_Z;command=io.KeyShift?Command::Redo:Command::Undo;}
    else if(io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y,ImGuiInputFlags_None,ImGuiKeyOwner_NoOwner)){key=ImGuiKey_Y;command=Command::Redo;}
    else if(allowDuplicate && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D,ImGuiInputFlags_None,ImGuiKeyOwner_NoOwner)){key=ImGuiKey_D;command=Command::Duplicate;}
    else if(allowDelete && !io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Delete,ImGuiInputFlags_None,ImGuiKeyOwner_NoOwner)){key=ImGuiKey_Delete;command=Command::Delete;}
    // Consume even if the focused document has no history or rejects the
    // command: that event must not fall through to a different document.
    if(command!=Command::None)ImGui::SetKeyOwner(key,root->ID,ImGuiInputFlags_LockThisFrame);
    return command;
}
}
