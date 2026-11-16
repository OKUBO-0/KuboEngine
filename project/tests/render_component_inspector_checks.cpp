#include "engine/editor/RenderComponentInspector.h"
#include "engine/editor/EntityHistory.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

static void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){ImGui::CreateContext();try{
    using namespace Engine;Scene::EntityWorld world;auto id=world.CreateEntity("Inspector target");
    Editor::EntityHistory history;std::string error;bool material=false,disabled=false;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1200,1000};io.DeltaTime=1.f/60;
    unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    Need(pixels && width>0 && height>0,"Missing ImGui font atlas");
    const auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1100,950});
        ImGui::Begin("Render inspector fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginDisabled(disabled);ImGui::SetNextItemOpen(true);
        if(material)Editor::DrawMaterialInspector(world,id,error);else Editor::DrawLightInspector(world,id,error);
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        const ImVec2 center{(a.x+b.x)/2,(a.y+b.y)/2};ImGui::EndDisabled();ImGui::End();ImGui::Render();return center;
    };
    const auto click=[&]{
        frame();const auto center=frame();ImGui::FocusWindow(ImGui::FindWindowByName("Render inspector fixture"));
        io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    };
    for(bool selected:{false,true}){
        material=selected;const auto absent=world.Capture().dump();frame();frame();
        Need(world.Capture().dump()==absent,"Passive Inspector changed World");
        disabled=true;click();Need(world.Capture().dump()==absent,"Disabled Add changed World");disabled=false;
        const auto before=world.Capture();click();
        Need(material?world.Find(id)->hasMaterial:world.Find(id)->hasLight,"Actual Add button did not create component");
        Need(history.Record(before,world,id,id,error),"Add history admission failed");history.Commit();
        const auto added=world.Capture().dump();Need(history.Undo(world,id,error) && world.Capture().dump()==absent,"Add Undo failed");
        Need(history.Redo(world,id,error) && world.Capture().dump()==added,"Add Redo failed");
        disabled=true;click();Need(world.Capture().dump()==added,"Disabled Remove changed World");disabled=false;
        const auto beforeRemove=world.Capture();click();
        Need(!(material?world.Find(id)->hasMaterial:world.Find(id)->hasLight),"Actual Remove button did not remove component");
        Need(history.Record(beforeRemove,world,id,id,error),"Remove history admission failed");history.Commit();
        Need(history.Undo(world,id,error) && world.Capture().dump()==added,"Remove Undo failed");
        Need(history.Redo(world,id,error) && world.Capture().dump()==absent,"Remove Redo failed");history.Clear();
    }
    Need(error.empty(),"Inspector reported an unexpected error");
    ImGui::DestroyContext();std::cout<<"PASS: actual Light/Material Inspector ImGui Add/Remove mouse events, disabled guards, passive frames, EntityHistory Undo/Redo; no physical-input or Game document-routing claim\n";return 0;
}catch(const std::exception& exception){ImGui::DestroyContext();std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
