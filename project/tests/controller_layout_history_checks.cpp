#include "engine/editor/AnimatorControllerEdits.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using namespace Engine::Editor;using Canvas=AnimatorControllerCanvasState;using Kind=Canvas::Kind;
    AnimatorControllerDocument source;std::string error;Need(AnimatorControllerEdits(source).New(error),error.c_str());const auto text=source.Text();AnimatorControllerDocument doc;Need(doc.Load(text,error),error.c_str());
    const auto key=Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[0].states[0]);const auto initial=doc.Geometry();auto moved=initial;Need(moved.Store(key,{700,-130}),"Move fixture failed");
    auto fractional=initial;fractional.Store(key,{180.75f,.5f});const auto passive=doc;Need(doc.ApplyLayout(fractional,error) && doc.HasSameState(passive),"Renderer pixel quantization created a passive command");
    Need(doc.ApplyLayout(moved,error) && !doc.IsDirty() && doc.Text()==text && doc.UndoCount()==1 && doc.Geometry()==moved,"Pure layout changed source dirty/baseline or failed to record");
    auto state=doc.Definition().layers[0].states[0];state.name="Renamed";Need(AnimatorControllerEdits(doc).ReplaceState(0,0,state,error),error.c_str());const auto renamed=doc.Text();auto movedAgain=moved;Need(movedAgain.Store(key,{900,80}) && doc.ApplyLayout(movedAgain,error),error.c_str());
    Need(doc.UndoCount()==3 && doc.Undo(error) && doc.Geometry()==moved && doc.Text()==renamed && doc.IsDirty(),"Undo layout skipped graph order");
    Need(doc.Undo(error) && doc.Geometry()==moved && doc.Text()==text && !doc.IsDirty(),"Undo graph lost layout or saved baseline");
    Need(doc.Undo(error) && doc.Geometry()==initial && !doc.IsDirty(),"Undo first move failed");
    Need(doc.Redo(error) && doc.Redo(error) && doc.Redo(error) && doc.Geometry()==movedAgain && doc.Text()==renamed,"Mixed graph/layout Redo failed");
    const auto noOp=doc;auto selectionOnly=doc.Geometry();selectionOnly.Select(doc.AuthoringIds(),0,0);Need(doc.ApplyLayout(selectionOnly,error) && doc.HasSameState(noOp),"Selection/passive presentation polluted history");
    Need(doc.HistoryBytes()>=initial.Bytes()+moved.Bytes()+text.size()*2,"Geometry escaped history budget");
    AnimatorControllerDocument::PreparedSave receipt;Need(doc.PrepareSave(receipt,error) && doc.CommitSaved(receipt) && !doc.IsDirty(),"Save baseline failed");
    Need(doc.Undo(error) && !doc.IsDirty() && doc.Undo(error) && doc.IsDirty(),"Saved graph baseline confused layout history");
    auto branch=doc.Geometry();Need(branch.Store(key,{11,22}) && doc.ApplyLayout(branch,error) && !doc.RedoCount(),"Layout branch did not retire Redo");
    Need(AnimatorControllerEdits(doc).AddState(0,{"Unused","default",1,true},error),error.c_str());const auto added=Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[0].states[1]);auto addedPose=doc.Geometry();Need(addedPose.Store(added,{333,444}) && doc.ApplyLayout(addedPose,error),error.c_str());
    Need(AnimatorControllerEdits(doc).RemoveState(0,1,std::nullopt,error) && doc.Geometry().PositionCount()==2 && doc.Undo(error) && doc.Geometry().Position(added,{})==Canvas::Point{333,444},"Deleted-node geometry did not follow graph memento");
    auto generic=doc.Definition();generic.layers[0].weight=.5f;Need(doc.Apply(generic,error),error.c_str());const auto freshKey=Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[0].states[0]);Need(doc.Geometry().Position(freshKey,{})==Canvas::Point{180,0},"Unknown graph reused foreign geometry");
    AnimatorControllerDocument baseline;Need(baseline.Load(text,error),error.c_str());const auto baselineKey=Canvas::Key(Kind::StateNode,baseline.AuthoringIds().layers[0].states[0]);auto preferred=baseline.Geometry();preferred.Store(baselineKey,{50,60});Need(baseline.InitializeLayout(preferred,error) && !baseline.UndoCount() && !baseline.IsDirty() && baseline.Geometry()==preferred,"Initial preferences became authoring edit");
    auto current=preferred;current.Store(baselineKey,{70,80});Need(baseline.ApplyLayout(current,error) && !baseline.InitializeLayout(preferred,error),"Baseline rewrite bypassed live history");
    AnimatorControllerDocument tiny(2,1);Need(tiny.Load(text,error),error.c_str());auto tooBig=tiny.Geometry();tooBig.Store(baselineKey,{90,90});const auto heldTiny=tiny;Need(!tiny.ApplyLayout(tooBig,error) && tiny.HasSameState(heldTiny),"Over-budget layout partially published");
    AnimatorControllerDocument bounded(2);Need(bounded.Load(text,error),error.c_str());for(int i=0;i<3;++i){auto pose=bounded.Geometry();pose.Store(baselineKey,{float(i),0});Need(bounded.ApplyLayout(pose,error),error.c_str());}Need(bounded.UndoCount()==2 && bounded.Undo(error) && bounded.Undo(error) && !bounded.Undo(error),"Layout commands bypassed command cap");
    auto candidate=doc.Geometry();candidate.Store(freshKey,{122,244});auto counted=doc;counting=true;allocations=0;const bool accepted=counted.ApplyLayout(candidate,error);counting=false;Need(accepted,"Counted move failed");const auto positions=allocations;
    for(size_t i=0;i<positions;++i){auto tested=doc;failAfter=static_cast<long>(i);const bool result=tested.ApplyLayout(candidate,error);failAfter=-1;Need(!result && tested.HasSameState(doc),"Failed move published graph/layout/IDs/history/baseline/revision");}
    std::cout<<"LAYOUT_HISTORY_ALLOCATION_POSITIONS="<<positions<<'\n';
    Need(doc.Text().find("layout")==std::string::npos,"Editor geometry leaked into runtime text");
    std::cout<<"PASS: unified graph/layout history ordering, source dirty separation, saved baseline, no-op selection, budget/cap/branch isolation, deleted-node restore, unknown identity defaults, initial preferences and allocation publication isolation\n";return 0;
}catch(const std::exception& e){failAfter=-1;std::cerr<<e.what()<<'\n';return 1;}}
