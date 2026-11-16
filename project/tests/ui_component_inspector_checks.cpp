#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/editor/UIComponentInspector.h"
#include "engine/editor/EntityHistory.h"
#include <imgui_internal.h>
#include <iostream>
static void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){ImGui::CreateContext();try{
    using namespace Engine;Scene::EntityWorld world;auto id=world.CreateEntity("UI target");Editor::EntityHistory history;Assets::AssetRegistry registry;
    std::string error;bool sprite=false,disabled=false;auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1200,1000};io.DeltaTime=1.f/60;
    unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);Need(pixels && width && height,"Missing fonts");
    const auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1100,950});ImGui::Begin("UI Inspector fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginDisabled(disabled);ImGui::SetNextItemOpen(true);
        if(sprite)Editor::DrawSpriteInspector(world,id,registry,std::filesystem::current_path(),error);else Editor::DrawCanvasInspector(world,id,error);
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec2 center{(a.x+b.x)/2,(a.y+b.y)/2};ImGui::EndDisabled();ImGui::End();ImGui::Render();return center;
    };
    const auto click=[&]{frame();const auto center=frame();ImGui::FocusWindow(ImGui::FindWindowByName("UI Inspector fixture"));io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
    for(bool selected:{false,true}){
        sprite=selected;const auto absent=world.Capture().dump();frame();frame();Need(world.Capture().dump()==absent,"Passive UI Inspector mutated World");
        disabled=true;click();Need(world.Capture().dump()==absent,"Disabled UI Add mutated World");disabled=false;
        const auto before=world.Capture();click();Need(sprite?world.Find(id)->hasSprite:world.Find(id)->hasCanvas,"UI Add mouse failed");
        Need(history.Record(before,world,id,id,error),error.c_str());history.Commit();const auto added=world.Capture().dump();
        Need(history.Undo(world,id,error) && world.Capture().dump()==absent && history.Redo(world,id,error) && world.Capture().dump()==added,"UI Add Undo/Redo failed");
        disabled=true;click();Need(world.Capture().dump()==added,"Disabled UI Remove mutated World");disabled=false;
        const auto beforeRemove=world.Capture();click();Need(!(sprite?world.Find(id)->hasSprite:world.Find(id)->hasCanvas),"UI Remove mouse failed");
        Need(history.Record(beforeRemove,world,id,id,error),error.c_str());history.Commit();
        Need(history.Undo(world,id,error) && world.Capture().dump()==added && history.Redo(world,id,error) && world.Capture().dump()==absent,"UI Remove Undo/Redo failed");history.Clear();
    }
    Need(error.empty(),"Unexpected UI Inspector error");ImGui::DestroyContext();std::cout<<"PASS: real ImGui Canvas/Sprite Add/Remove mouse events, passive/disabled guards and grouped EntityHistory Undo/Redo (not Game owner, image picker or physical UI)\n";return 0;
}catch(const std::exception& e){ImGui::DestroyContext();std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
