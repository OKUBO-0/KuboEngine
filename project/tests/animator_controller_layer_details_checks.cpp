#include "engine/editor/AnimatorControllerLayerDetails.h"
#include <iostream>

static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    ImGui::CreateContext();
    try {
        using namespace Engine::Editor;
        AnimatorControllerDocument document;std::string error;ControllerLayerDetailsView draft;
        Need(AnimatorControllerEdits(document).New(error),"New failed");
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize={1280,1024};io.DeltaTime=1.0f/60;
        unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        Need(pixels && width>0 && height>0,"Font atlas missing");
        const auto draw=[&](bool enabled,size_t layer){
            const auto before=document;
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1200,1000});
            ImGui::Begin("Layer fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
            const auto snapshot=document.Definition();AnimatorControllerEdits edit(document);
            DrawControllerLayerDetails(snapshot,document.AuthoringIds(),layer,edit,enabled,document.Revision(),error,draft);
            ImGui::End();ImGui::Render();
            Need(document.HasSameState(before),"Passive rendering changed document/history");
            Need(ImGui::GetDrawData()!=nullptr,"Draw data missing");
        };
        draw(true,0);draw(false,0);
        auto layer=document.Definition().layers[0];layer.name="Upper###literal";
        layer.mode="additive";layer.weight=.25f;layer.mask={"Hand","Spine"};
        Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),"Layer replacement failed");
        const auto edited=document;draw(true,0);draw(false,0);
        Need(document.Undo(error) && document.Definition().layers[0].mode=="override", "Undo failed");
        Need(document.Redo(error) && document.Text()==edited.Text(),"Redo failed");
        const auto reject=[&](auto candidate){const auto before=document;
            Need(!AnimatorControllerEdits(document).ReplaceLayer(0,candidate,error) && document.HasSameState(before),"Invalid layer changed document");};
        auto bad=layer;bad.weight=1.1f;reject(bad);bad=layer;bad.weight=-.1f;reject(bad);
        bad=layer;bad.mode="multiply";reject(bad);bad=layer;bad.mask={"Hand","Hand"};reject(bad);
        bad=layer;bad.mask={""};reject(bad);bad=layer;bad.name=std::string(129,'a');reject(bad);
        AnimatorControllerEdits stale(document);layer.weight=.5f;
        Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),"Weight failed");
        const auto stable=document;Need(!stale.RemoveLayer(0,error) && document.HasSameState(stable),"Stale edit changed document");
        layer.mask.clear();for(int i=0;i<256;++i)layer.mask.push_back("Bone"+std::to_string(i));
        Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),"Maximum mask refused");
        draft.bone=std::string(128,'b');draw(true,0);draw(false,0);
        bad=layer;bad.mask.push_back("Overflow");reject(bad);
        for(int i=1;i<4;++i){auto next=layer;next.name="Layer "+std::to_string(i);Need(AnimatorControllerEdits(document).AddLayer(next,error),"Add layer failed");}
        draw(true,3);draw(false,3);
        const auto full=document;layer.name="Fifth";
        Need(!AnimatorControllerEdits(document).AddLayer(layer,error) && document.HasSameState(full),"Fifth layer accepted");
        for(int i=3;i>0;--i)Need(AnimatorControllerEdits(document).RemoveLayer(i,error),"Remove layer failed");
        const auto last=document;Need(!AnimatorControllerEdits(document).RemoveLayer(0,error) && document.HasSameState(last),"Last layer deleted");
        draw(true,0);
        ImGui::DestroyContext();
        std::cout<<"PASS: Layer details real ImGui passive frames enabled/disabled/256 masks/4 layers; typed command validation, stale revision, Undo/Redo; no physical gesture or filesystem claims\n";
        return 0;
    }catch(const std::exception& exception){ImGui::DestroyContext();std::cerr<<exception.what()<<'\n';return 1;}
}
