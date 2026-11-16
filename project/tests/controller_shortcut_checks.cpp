#include "engine/editor/AnimatorControllerPanel.h"
#include "engine/editor/DocumentShortcuts.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){ImGui::CreateContext();try{
    using namespace Engine::Editor;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1600,1200};io.DeltaTime=1.f/60;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    {
        AnimatorControllerPanel panel;std::string error;Need(panel.New(error),error.c_str());
        using Command=DocumentShortcuts::Command;int sceneUndo=0,sceneRedo=0,duplicates=0,deletes=0;bool editable=true,sceneEnabled=true,modal=false;
        char textValue[128]="Text";ImRect textBox;bool leaked=false,showChild=false;
        const auto frame=[&]{ImGui::NewFrame();
            // Same ordering as production Game: Scene dispatch precedes Panel.
            for(const auto name:{"Entity Hierarchy","Entity Inspector","Scene"}){
                const auto command=DocumentShortcuts::Take(name,sceneEnabled,true,std::string_view(name)=="Entity Hierarchy");
                if(command==Command::Undo)++sceneUndo;else if(command==Command::Redo)++sceneRedo;else if(command==Command::Duplicate)++duplicates;
                if(command==Command::Delete)++deletes;
            }
            int y=0;for(const auto name:{"Entity Hierarchy","Entity Inspector","Scene","Git"}){
                ImGui::SetNextWindowPos({900,float(y)},ImGuiCond_Always);ImGui::SetNextWindowSize({450,180},ImGuiCond_Always);y+=190;
                ImGui::Begin(name);if(std::string(name)=="Git"){
                    ImGui::InputText("Local text",textValue,sizeof(textValue));textBox={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
                    if(modal && !ImGui::IsPopupOpen("Shortcut modal"))ImGui::OpenPopup("Shortcut modal");
                    if(ImGui::BeginPopupModal("Shortcut modal",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Modal owns input");if(!modal)ImGui::CloseCurrentPopup();ImGui::EndPopup();}
                }else ImGui::TextUnformatted("Separate Scene document");ImGui::End();
            }
            ImGui::SetNextWindowPos({0,0},ImGuiCond_Always);panel.Draw(editable);
            if(showChild){ImGui::Begin("Controller Graph");ImGui::BeginChild("Shortcut child",{150,40});ImGui::TextUnformatted("Child focus fixture");ImGui::EndChild();ImGui::End();}
            if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z,false))leaked=true;
            ImGui::Render();};
        const auto focus=[&](const char* name){auto* window=ImGui::FindWindowByName(name);Need(window!=nullptr,"Missing focus fixture window");ImGui::FocusWindow(window);ImGui::ClearActiveID();frame();};
        const auto press=[&](ImGuiKey key,bool shift=false,bool alt=false,bool super=false,bool ctrl=true){
            io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddKeyEvent(ImGuiMod_Alt,alt);io.AddKeyEvent(ImGuiMod_Super,super);frame();
            io.AddKeyEvent(key,true);frame();io.AddKeyEvent(key,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);io.AddKeyEvent(ImGuiMod_Alt,false);io.AddKeyEvent(ImGuiMod_Super,false);frame();};
        frame();frame();const auto original=panel.Session().Document()->Definition().layers[0].states[0];
        auto changed=original;changed.name="Shortcut edit";Need(AnimatorControllerEdits(*panel.Session().Document()).ReplaceState(0,0,changed,error),error.c_str());frame();
        focus("Controller Graph");leaked=false;press(ImGuiKey_Z);
        Need(panel.Session().Document()->Definition().layers[0].states[0].name==original.name,"Focused Controller Ctrl+Z did not undo its own document");
        Need(sceneUndo==0 && sceneRedo==0 && !leaked,"Controller event changed/leaked to Scene");
        press(ImGuiKey_Y);Need(panel.Session().Document()->Definition().layers[0].states[0].name==changed.name,"Controller Ctrl+Y did not redo");
        press(ImGuiKey_Z);press(ImGuiKey_Z,true);Need(panel.Session().Document()->Definition().layers[0].states[0].name==changed.name,"Controller Ctrl+Shift+Z did not redo");
        for(const auto name:{"Entity Hierarchy","Entity Inspector","Scene"}){focus(name);const auto before=sceneUndo;press(ImGuiKey_Z);Need(sceneUndo==before+1,"Focused Scene root did not own Undo");Need(panel.Session().Document()->Definition().layers[0].states[0].name==changed.name,"Scene key changed visible Controller");press(ImGuiKey_Y);press(ImGuiKey_D);}
        Need(sceneRedo==3 && duplicates==3,"Scene Redo/Duplicate routing failed");
        for (const auto name : {"Scene", "Entity Inspector", "Controller Graph"}) {
            focus(name);press(ImGuiKey_Delete,false,false,false,false);
        }
        focus("Entity Hierarchy");press(ImGuiKey_Delete);press(ImGuiKey_Delete,true,false,false,false);
        Need(deletes==0,"Delete escaped its unmodified Hierarchy binding");
        press(ImGuiKey_Delete,false,false,false,false);Need(deletes==1,"Hierarchy Delete did not run");
        modal=true;frame();frame();press(ImGuiKey_Delete,false,false,false,false);modal=false;frame();frame();
        Need(deletes==1,"Delete escaped a modal");
        const auto sceneBefore=sceneUndo;focus("Git");press(ImGuiKey_Z);Need(sceneUndo==sceneBefore && panel.Session().Document()->Definition().layers[0].states[0].name==changed.name,"Unrelated tool key changed a document");
        focus("Controller Graph");const auto undoCount=panel.Session().Document()->UndoCount();
        press(ImGuiKey_Z,false,true);press(ImGuiKey_Z,false,false,true);press(ImGuiKey_D);Need(panel.Session().Document()->UndoCount()==undoCount && duplicates==3,"Extra modifier/Controller Duplicate executed history");
        editable=false;press(ImGuiKey_Z);editable=true;Need(panel.Session().Document()->UndoCount()==undoCount && sceneUndo==sceneBefore,"Disabled Controller fell through to Scene");
        sceneEnabled=false;focus("Scene");press(ImGuiKey_Z);sceneEnabled=true;Need(sceneUndo==sceneBefore,"Busy/disabled Scene consumed history");
        focus("Controller Graph");io.AddMousePosEvent(-100,-100);io.AddMouseButtonEvent(1,true);frame();press(ImGuiKey_Z);io.AddMouseButtonEvent(1,false);frame();Need(panel.Session().Document()->UndoCount()==undoCount,"Mouse gesture executed history");
        ImGui::SetWindowCollapsed("Controller Graph",true);frame();press(ImGuiKey_Z);ImGui::SetWindowCollapsed("Controller Graph",false);frame();Need(panel.Session().Document()->UndoCount()==undoCount,"Collapsed Controller executed history");
        showChild=true;frame();frame();auto* root=ImGui::FindWindowByName("Controller Graph");ImGuiWindow* child=nullptr;
        for(auto* window:ImGui::GetCurrentContext()->Windows)if(window!=root && window->Active && ImGui::IsWindowChildOf(window,root,false,false)){child=window;break;}
        Need(child!=nullptr,"Ordinary child focus fixture missing");ImGui::FocusWindow(child);ImGui::ClearActiveID();frame();press(ImGuiKey_Z);Need(panel.Session().Document()->Definition().layers[0].states[0].name==original.name,"Child focus lost Controller history");press(ImGuiKey_Y);
        focus("Git");const auto center=textBox.GetCenter();io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        io.AddInputCharactersUTF8(" typed");frame();const std::string typed=textValue;Need(typed!="Text" && ImGui::IsAnyItemActive(),"Real text edit not active");press(ImGuiKey_Z);Need(std::string(textValue)!=typed && sceneUndo==sceneBefore && panel.Session().Document()->UndoCount()==undoCount,"Text Undo escaped text widget");
        ImGui::ClearActiveID();modal=true;frame();frame();press(ImGuiKey_Z);modal=false;frame();frame();Need(sceneUndo==sceneBefore && panel.Session().Document()->UndoCount()==undoCount,"Popup key escaped to document");
        focus("Controller Graph");io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddKeyEvent(ImGuiKey_Z,true);frame();const auto heldUndo=panel.Session().Document()->UndoCount();for(int i=0;i<60;++i)frame();Need(panel.Session().Document()->UndoCount()==heldUndo,"Held key repeated destructive history");io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
        while(panel.Session().Document()->UndoCount())Need(panel.UndoEdit(error),error.c_str());frame();focus("Controller Graph");press(ImGuiKey_Z);Need(sceneUndo==sceneBefore,"Empty Controller history fell through to Scene");
    }
    ImGui::DestroyContext();std::cout<<"PASS: real Panel Ctrl+Z/Y/Shift+Z, child focus, Scene roots/duplicate/visible peer isolation, text Undo, popup/mouse/disabled/collapsed/modifier gates, held-key and empty-history no fallback\n";return 0;
}catch(const std::exception& exception){ImGui::DestroyContext();std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
