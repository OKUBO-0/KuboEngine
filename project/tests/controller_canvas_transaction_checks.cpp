#include "engine/editor/AnimatorControllerPanel.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
namespace Engine::Editor {
class ControllerCanvasChecksAccess {
public:
    using Canvas=AnimatorControllerCanvasState;
    struct Snapshot {
        AnimatorControllerDocument document;Canvas canvas,baseline;std::vector<uint64_t> nodes;
        ControllerNodes::EditorContext* context;const AnimatorControllerDocument* canvasDocument;uint64_t canvasRevision,geometryOwner,geometryRevision,owner;bool positioned;
        std::vector<Canvas::Point> positions;
        explicit Snapshot(AnimatorControllerPanel& panel):document(*panel.session_.Document()),canvas(panel.canvasState_),baseline(panel.geometryBaseline_),nodes(panel.canvasNodes_),context(panel.context_),canvasDocument(panel.canvasDocument_),canvasRevision(panel.canvasRevision_),geometryOwner(panel.geometryOwner_),geometryRevision(panel.geometryRevision_),owner(panel.session_.GuiOwnerEpoch()),positioned(panel.positioned_){
            if(context){AnimatorControllerPanel::Current current(context);for(const auto id:nodes){const auto p=ControllerNodes::GetNodePosition(id);positions.push_back({p.x,p.y});}}
        }
        bool Same(AnimatorControllerPanel& panel) const {
            if(!panel.session_.Document()->HasSameState(document) || panel.canvasState_!=canvas || panel.geometryBaseline_!=baseline || panel.canvasNodes_!=nodes || panel.context_!=context || panel.canvasDocument_!=canvasDocument || panel.canvasRevision_!=canvasRevision || panel.geometryOwner_!=geometryOwner || panel.geometryRevision_!=geometryRevision || panel.session_.GuiOwnerEpoch()!=owner || panel.positioned_!=positioned)return false;
            if(context){AnimatorControllerPanel::Current current(context);for(size_t i=0;i<nodes.size();++i){const auto p=ControllerNodes::GetNodePosition(nodes[i]);if(positions[i]!=Canvas::Point{p.x,p.y})return false;}}
            return true;
        }
    };
    static void Move(AnimatorControllerPanel& panel,float x){Need(panel.context_!=nullptr,"Transaction fixture has no context");AnimatorControllerPanel::Current current(panel.context_);const auto& layer=panel.session_.Document()->AuthoringIds().layers[0];ControllerNodes::SetNodePosition(Canvas::Key(Canvas::Kind::StateNode,layer.states[0]),{x,42});ControllerNodes::SetNodePosition(Canvas::Key(Canvas::Kind::AnyNode,layer.id),{-22,-33});}
};
}
int main(){ImGui::CreateContext();try {
    using namespace Engine::Editor;using Access=ControllerCanvasChecksAccess;using Canvas=AnimatorControllerCanvasState;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1200,1000};io.DeltaTime=1.f/60;unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    std::string error;const auto frame=[&](AnimatorControllerPanel& panel){ImGui::NewFrame();panel.Draw(true);ImGui::Render();};
    const auto setup=[&](AnimatorControllerPanel& panel,int mode){
        Need(panel.New(error),error.c_str());AnimatorControllerDocument::PreparedSave saved;Need(panel.Session().Document()->PrepareSave(saved,error) && panel.Session().Document()->CommitSaved(saved),"Transaction clean baseline failed");frame(panel);frame(panel);
        if(mode==1 || mode==5){auto state=panel.Session().Document()->Definition().layers[0].states[0];state.name="Graph command";Need(AnimatorControllerEdits(*panel.Session().Document()).ReplaceState(0,0,state,error),error.c_str());if(mode==1)frame(panel);}
        if(mode==2 || mode==3 || mode==4 || mode==6){Access::Move(panel,700);Need(panel.CommitCanvasLayout(error),error.c_str());if(mode>=3){Need(panel.Session().Document()->Undo(error),error.c_str());if(mode!=4)frame(panel);}}
        if(mode==0 || mode==1 || mode==5 || mode==6)Access::Move(panel,900);
    };
    for(int mode=0;mode<7;++mode){
        const auto operation=[&](AnimatorControllerPanel& panel){if(mode==1 || mode==2)return panel.UndoEdit(error);if(mode==3 || mode==6)return panel.RedoEdit(error);return panel.CommitCanvasLayout(error);};
        size_t count=0;
        {AnimatorControllerPanel panel;setup(panel,mode);const Access::Snapshot before(panel);allocations=0;counting=true;const bool result=operation(panel);counting=false;count=allocations;
            if(mode==6)Need(!result && before.Same(panel),"Pending Redo silently committed/retired old branch");
            else {Need(result,error.c_str());auto& doc=*panel.Session().Document();const auto node=Canvas::Key(Canvas::Kind::StateNode,doc.AuthoringIds().layers[0].states[0]);
                if(mode==0 || mode==5)Need(doc.Geometry().Position(node,{})==Canvas::Point{900,42},"Commit lost latest native preview");
                if(mode==1)Need(doc.Definition().layers[0].states[0].name=="Graph command" && doc.RedoCount()==1 && doc.Geometry().Position(node,{})==Canvas::Point{180,0},"Pending Undo crossed graph order");
                if(mode==2 || mode==4)Need(doc.Geometry().Position(node,{})==Canvas::Point{180,0},"Undo/synchronization geometry wrong");
                if(mode==3)Need(doc.Geometry().Position(node,{})==Canvas::Point{700,42},"Redo geometry wrong");
            }
        }
        for(size_t i=0;i<count;++i){AnimatorControllerPanel panel;setup(panel,mode);const Access::Snapshot before(panel);failAfter=static_cast<long>(i);const bool result=operation(panel);failAfter=-1;Need(!result && before.Same(panel),"Panel transaction failure published partial document/preview/baseline/native context");}
        std::cout<<"CANVAS_TRANSACTION_MODE="<<mode<<" ALLOCATION_POSITIONS="<<count<<'\n';
    }
    {AnimatorControllerPanel panel;setup(panel,0);Need(panel.CommitCanvasLayout(error),error.c_str());const Access::Snapshot before(panel);counting=true;allocations=0;const bool result=panel.CommitCanvasLayout(error);counting=false;Need(result && allocations==0 && before.Same(panel),"Unchanged commit allocated or changed state");}
    ImGui::DestroyContext();std::cout<<"PASS: complete Panel commit/Undo+pending/Undo/Redo/external-Undo sync/graph+native-preview/pending-Redo refusal allocation sweeps preserve document, cached preview, baseline, context identity and native positions; zero-allocation passive commit (no file publication/physical input QA)\n";return 0;
}catch(const std::exception& e){failAfter=-1;ImGui::DestroyContext();std::cerr<<e.what()<<'\n';return 1;}}
