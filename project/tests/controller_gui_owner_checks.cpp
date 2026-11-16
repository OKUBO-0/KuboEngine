#include "engine/editor/AnimatorControllerPanel.h"
#include <imgui_internal.h>
#include <map>
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static std::map<ImGuiID,ImRect> boxes;static std::map<std::string,std::pair<ImGuiID,ImRect>> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& bb,const ImGuiLastItemData*){boxes.insert_or_assign(id,bb);}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(auto found=boxes.find(id);found!=boxes.end()){if(std::string(label)=="Bone (Enter)")items.try_emplace(label,std::make_pair(id,found->second));else items.insert_or_assign(label,std::make_pair(id,found->second));}}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return nullptr;}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
namespace Engine::Editor {
class ControllerCanvasChecksAccess {
public:
    static void Layer(AnimatorControllerPanel& panel,size_t index){panel.RetireGui();panel.layer_=index;panel.state_=0;panel.canvasState_.Select(panel.session_.Document()->AuthoringIds(),index,0);}
    static const std::string& BoneDraft(const AnimatorControllerPanel& panel){return panel.layerDetails_.bone;}
    static void Move(AnimatorControllerPanel& panel,bool any,float x,float y){Need(panel.context_!=nullptr,"History fixture lost node context");AnimatorControllerPanel::Current current(panel.context_);const auto& layer=panel.session_.Document()->AuthoringIds().layers[0];const auto key=AnimatorControllerCanvasState::Key(any?AnimatorControllerCanvasState::Kind::AnyNode:AnimatorControllerCanvasState::Kind::StateNode,any?layer.id:layer.states[0]);ControllerNodes::SetNodePosition(key,{x,y});}
};
}
int main(){ImGui::CreateContext();try{
    using namespace Engine::Editor;using namespace Engine::AnimationSystem::Controller;
    using Canvas=AnimatorControllerCanvasState;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1800,2400};io.DeltaTime=1.f/60;unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);ImGui::GetCurrentContext()->TestEngineHookItems=true;
    {
    AnimatorControllerPanel panel;Graph graph;graph.parameters={{"Parameter",Kind::Float,1,false}};Layer layer;layer.name="Base";layer.mode="override";layer.states={{"Idle","default",1,true}};layer.mask={"First","Last"};graph.layers.push_back(layer);layer.name="Overlay";graph.layers.push_back(layer);std::string text,error;Need(AnimatorControllerSerialization::Encode(graph,text,error),error.c_str());
    const auto replace=[&]{Need(panel.Session().NewFromText(text,error,true),error.c_str());panel.ShowPreparedDraft();};replace();
    const auto frame=[&]{items.clear();boxes.clear();ImGui::NewFrame();ImGui::SetWindowSize("Controller Graph",{1600,2200},ImGuiCond_Always);ImGui::SetNextWindowPos({0,0},ImGuiCond_Always);panel.Draw(true);ImGui::Render();};
    const auto click=[&](const char* label){frame();frame();const auto found=items.find(label);if(found==items.end())throw std::runtime_error(std::string("Missing real Panel widget: ")+label);const auto p=found->second.second.GetCenter();io.AddMousePosEvent(p.x,p.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
    const auto type=[&](const char* label){click(label);io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddInputCharactersUTF8("Stale input");frame();};
    const auto enter=[&]{io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();};
    for(const auto label:{"State name (Enter)","Name (Enter)","Layer name (Enter)","Bone (Enter)"}){
        replace();frame();type(label);const auto oldId=items.at(label).first;const auto ids=panel.Session().Document()->AuthoringIds();const auto revision=panel.Session().Document()->Revision(),owner=panel.Session().GuiOwnerEpoch();
        replace();Need(panel.Session().Document()->AuthoringIds()==ids && panel.Session().Document()->Revision()==revision && panel.Session().GuiOwnerEpoch()>owner,"Equal-ID/revision owner replacement fixture ineffective");frame();frame();Need(items.at(label).first!=oldId,"Distinct document reused active widget ID");enter();Need(panel.Session().Document()->Text()==text && panel.Session().Document()->UndoCount()==0,"Previous document input changed new owner");
    }
    replace();frame();type("Layer name (Enter)");const auto baseWidget=items.at("Layer name (Enter)").first;ControllerCanvasChecksAccess::Layer(panel,1);frame();frame();Need(items.at("Layer name (Enter)").first!=baseWidget,"Layer change reused name widget");enter();Need(panel.Session().Document()->Definition().layers[1].name=="Overlay","Old layer input renamed another layer");
    type("New mask bone");Need(!ControllerCanvasChecksAccess::BoneDraft(panel).empty(),"Mask scratch input not staged");ControllerCanvasChecksAccess::Layer(panel,0);frame();Need(ControllerCanvasChecksAccess::BoneDraft(panel).empty(),"Mask scratch draft leaked to another layer");
    type("Bone (Enter)");auto changed=panel.Session().Document()->Definition().layers[0];changed.mask.erase(changed.mask.begin());Need(AnimatorControllerEdits(*panel.Session().Document()).ReplaceLayer(0,changed,error),error.c_str());frame();frame();enter();Need(panel.Session().Document()->Definition().layers[0].mask==std::vector<std::string>{"Last"},"Compacted mask accepted retired input buffer");
    replace();frame();type("State name (Enter)");const auto owner=panel.Session().GuiOwnerEpoch();Need(!panel.Session().NewFromText("{}",error,true) && panel.Session().GuiOwnerEpoch()==owner,"Failed replacement retired owner");enter();Need(panel.Session().Document()->Definition().layers[0].states[0].name=="Stale input","Failed replacement discarded still-owned active edit");
    Need(panel.Close(true,error) && panel.New(error) && panel.Session().GuiOwnerEpoch()>owner,"Panel Close/New reused owner");frame();
    // Allocation failures during recovered document construction cannot publish
    // either a new document pointer or an epoch. No GUI calls under injection.
    counting=true;allocations=0;const bool counted=panel.Session().NewFromText(text,error,true);counting=false;Need(counted,"Counted owner replacement failed");const auto count=allocations;const auto held=*panel.Session().Document();const auto* pointer=panel.Session().Document();const auto heldEpoch=panel.Session().GuiOwnerEpoch();
    for(size_t i=0;i<count;++i){failAfter=static_cast<long>(i);const bool result=panel.Session().NewFromText(text,error,true);failAfter=-1;Need(!result && panel.Session().Document()==pointer && panel.Session().GuiOwnerEpoch()==heldEpoch && panel.Session().Document()->HasSameState(held),"Allocation failure published GUI owner or document");}
    panel.ShowPreparedDraft();std::cout<<"GUI_OWNER_ALLOCATION_POSITIONS="<<count<<'\n';
    replace();AnimatorControllerDocument::PreparedSave baselineSave;Need(panel.Session().Document()->PrepareSave(baselineSave,error) && panel.Session().Document()->CommitSaved(baselineSave),"History baseline fixture failed");frame();frame();
    auto& history=*panel.Session().Document();const auto geometry=history.Geometry();const auto node=Canvas::Key(Canvas::Kind::StateNode,history.AuthoringIds().layers[0].states[0]);
    io.AddMousePosEvent(1750,2350);io.AddMouseButtonEvent(0,true);frame();ControllerCanvasChecksAccess::Move(panel,false,500,50);ControllerCanvasChecksAccess::Move(panel,true,-40,-30);frame();ControllerCanvasChecksAccess::Move(panel,false,600,70);frame();
    Need(!history.UndoCount() && history.Geometry()==geometry,"Drag previews generated per-frame history");io.AddMouseButtonEvent(0,false);frame();
    Need(history.UndoCount()==1 && !history.IsDirty() && history.Geometry().Position(node,{})==Canvas::Point{600,70},"Released multi-node gesture was not one clean layout command");const auto movedGeometry=history.Geometry();
    click("Undo");Need(history.Geometry()==geometry && history.Text()==text && !history.IsDirty(),"Real Panel Undo did not restore geometry/source baseline");click("Redo");Need(history.Geometry()==movedGeometry && !history.IsDirty(),"Real Panel Redo did not restore geometry");
    auto renamed=history.Definition().layers[0].states[0];renamed.name="Mixed history";Need(AnimatorControllerEdits(history).ReplaceState(0,0,renamed,error),error.c_str());frame();click("Undo");Need(history.Text()==text && history.Geometry()==movedGeometry,"Graph Undo crossed layout order");click("Undo");Need(history.Geometry()==geometry && !history.IsDirty(),"Second Undo did not reach preceding layout command");click("Redo");click("Redo");Need(history.Definition().layers[0].states[0].name==renamed.name && history.Geometry()==movedGeometry,"Mixed Panel Redo failed");
    const auto commands=history.UndoCount();io.AddMousePosEvent(1750,2350);io.AddMouseButtonEvent(0,true);frame();ControllerCanvasChecksAccess::Move(panel,false,800,90);frame();ControllerCanvasChecksAccess::Move(panel,false,600,70);frame();io.AddMouseButtonEvent(0,false);frame();Need(history.UndoCount()==commands,"No-op gesture polluted history");
    std::cout<<"PASS: actual Panel grouped multi-node gesture preview/release/one Undo+Redo, mixed graph ordering, clean layout baseline and no-op suppression (injected events/positions, not physical mouse QA)\n";
    }
    ImGui::DestroyContext();std::cout<<"PASS: real Panel injected input isolated across equal-ID+revision document replacement, layer switch and mask compaction; scratch draft reset; failed replacement retains active edit; Close/New epoch and allocation publication isolation (not physical input QA)\n";return 0;
}catch(const std::exception& e){failAfter=-1;ImGui::DestroyContext();std::cerr<<e.what()<<'\n';return 1;}}
