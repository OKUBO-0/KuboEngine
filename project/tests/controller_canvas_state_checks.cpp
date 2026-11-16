#include "engine/editor/AnimatorControllerCanvasState.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <iostream>
#include <limits>
#include <set>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    using namespace Engine::Editor;using Canvas=AnimatorControllerCanvasState;using Kind=Canvas::Kind;
    AnimatorControllerDocument doc;std::string error;Need(AnimatorControllerEdits(doc).New(error),error.c_str());
    Need(AnimatorControllerEdits(doc).AddState(0,{"Middle","default",1,true},error),error.c_str());
    Need(AnimatorControllerEdits(doc).AddState(0,{"Last","default",1,true},error),error.c_str());
    const auto original=doc.AuthoringIds();Canvas canvas;size_t layer=0,state=2;canvas.Select(original,0,2);
    const auto key=Canvas::Key(Kind::StateNode,original.layers[0].states[2]);
    const Canvas::Point position{913.25f,-271.5f};Need(canvas.Store(key,position),"Store refused");
    auto renamed=doc.Definition().layers[0].states[2];renamed.name="Renamed###日本語";
    Need(AnimatorControllerEdits(doc).ReplaceState(0,2,renamed,error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);
    Need(state==2 && canvas.Position(key,{})==position,"Rename lost position/selection");
    Need(AnimatorControllerEdits(doc).RemoveState(0,1,std::nullopt,error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);
    Need(state==1 && canvas.Position(Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[0].states[1]),{})==position,"Compaction transferred presentation identity");
    Need(Canvas::Find(doc.AuthoringIds().layers[0].states,key,Kind::StateNode)==1,"Stable lookup failed");
    Need(!Canvas::Find(doc.AuthoringIds().layers[0].states,key,Kind::Input),"Wrong pin namespace accepted");
    Need(!Canvas::Find(doc.AuthoringIds().layers[0].states,Canvas::Key(Kind::StateNode,original.layers[0].states[1]),Kind::StateNode),"Retired node accepted");
    Need(doc.Undo(error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);Need(state==2,"Undo lost survivor selection");
    Need(doc.Redo(error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);Need(state==1,"Redo lost survivor selection");
    auto extra=doc.Definition().layers[0];extra.name="Overlay";Need(AnimatorControllerEdits(doc).AddLayer(extra,error),error.c_str());
    canvas.Select(doc.AuthoringIds(),1,1);const auto overlay=doc.AuthoringIds().layers[1].id;
    Need(AnimatorControllerEdits(doc).RemoveLayer(0,error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);
    Need(layer==0 && state==1 && doc.AuthoringIds().layers[0].id==overlay,"Layer compaction changed selection");
    Need(doc.Undo(error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);Need(layer==1 && state==1,"Layer Undo selection changed");
    // Removed nodes keep their positions for Undo, not for replacement nodes.
    canvas.Select(doc.AuthoringIds(),0,1);const auto deleted=Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[0].states[1]);
    Need(canvas.Store(deleted,{111,222}),"Deleted fixture position refused");
    Need(AnimatorControllerEdits(doc).RemoveState(0,1,std::nullopt,error),error.c_str());canvas.Resolve(doc.AuthoringIds(),layer,state);Need(state==0,"Missing selection did not fall back safely");
    Need(doc.Undo(error),error.c_str());Need(canvas.Position(deleted,{})==Canvas::Point{111,222},"Undo position missing");
    std::set<uint64_t> unique;for(unsigned tag=1;tag<=6;++tag){const auto id=Canvas::Key(static_cast<Kind>(tag),AnimatorControllerAuthoringIds::MaximumId);Need(unique.insert(id).second && id<(uint64_t{1}<<53),"Canvas namespace alias/range");}
    const auto count=canvas.PositionCount();const auto beforeInvalid=canvas.Position(key,{});Need(!canvas.Store(key,{std::numeric_limits<float>::infinity(),0}) && !canvas.Store(key,{0,std::numeric_limits<float>::quiet_NaN()}) && !canvas.Store(Canvas::Key(Kind::Link,1),{}) && canvas.PositionCount()==count && canvas.Position(key,{})==beforeInvalid,"Invalid position mutated layout");
    Canvas bounded;for(size_t i=1;i<=Canvas::MaximumPositions+1;++i)Need(bounded.Store(Canvas::Key(Kind::StateNode,i),{float(i),0}),"Bounded store failed");
    Need(bounded.PositionCount()==Canvas::MaximumPositions && bounded.Position(Canvas::Key(Kind::StateNode,1),{-1,-1})==Canvas::Point{-1,-1},"Layout budget/eviction failed");
    canvas.Reset();Need(canvas.PositionCount()==0 && canvas.Position(key,{7,8})==Canvas::Point{7,8},"New document inherited layout");
    // Real node-editor contexts: high-bit namespace IDs must survive pointer-sized
    // wrappers and context destruction/recreation with SettingsFile disabled.
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,1024};io.DeltaTime=1.f/60;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    namespace Nodes=ax::NodeEditor;Nodes::Config config;config.SettingsFile=nullptr;
    const auto other=Canvas::Key(Kind::AnyNode,original.layers[0].id);
    const Canvas::Point pixelPosition{913,-271}; // node-editor quantizes to pixel coordinates
    for(int epoch=0;epoch<2;++epoch){
        auto* context=Nodes::CreateEditor(&config);Need(context!=nullptr,"Node context unavailable");Nodes::SetCurrentEditor(context);
        ImGui::NewFrame();ImGui::Begin("Canvas identity fixture");Nodes::Begin("Graph",{1000,600});
        const auto p=epoch?canvas.Position(key,{}):pixelPosition;Nodes::SetNodePosition(key,{p.x,p.y});Nodes::SetNodePosition(other,{-33,44});
        Nodes::BeginNode(key);ImGui::TextUnformatted("State");Nodes::BeginPin(Canvas::Key(Kind::Input,original.layers[0].states[2]),Nodes::PinKind::Input);ImGui::TextUnformatted("in");Nodes::EndPin();Nodes::EndNode();
        Nodes::BeginNode(other);ImGui::TextUnformatted("Any State");Nodes::EndNode();Nodes::SelectNode(key);
        Nodes::NodeId selected;Need(Nodes::GetSelectedNodes(&selected,1)==1 && selected.Get()==key,"High-bit selection identity truncated");
        Nodes::End();const auto p1=Nodes::GetNodePosition(key),p2=Nodes::GetNodePosition(other);
        std::cout<<"CANVAS_EPOCH="<<epoch<<" STATE="<<p1.x<<','<<p1.y<<" ANY="<<p2.x<<','<<p2.y<<'\n';
        Need(p1.x==pixelPosition.x && p1.y==pixelPosition.y && p2.x==-33 && p2.y==44,"Real canvas position alias/restore failed");Need(canvas.Store(key,{p1.x,p1.y}),"Capture failed");
        ImGui::End();ImGui::Render();Nodes::SetCurrentEditor(nullptr);Nodes::DestroyEditor(context);
    }
    ImGui::DestroyContext();
    std::cout<<"PASS: stable canvas namespaces/lookups/rename+compaction/Undo+Redo/layer selection/removed position retention/bounded finite layout/new owner reset/real node context rebuild and high-bit selection; no disk persistence or physical gesture claims\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
