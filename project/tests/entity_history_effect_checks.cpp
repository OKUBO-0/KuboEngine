#include "engine/editor/EntityHistory.h"
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
using namespace Engine::Editor;using World=Engine::Scene::EntityWorld;
void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
class Effect final:public EntityHistoryEffect {
public:
    int& external;bool fail=false,throwing=false,held=false;int begins=0,ends=0;
    std::function<void(bool)> observe;
    Effect(int& value,size_t bytes):EntityHistoryEffect(bytes),external(value){}
    bool Begin(bool undo,std::string& error) override{
        ++begins;if(observe)observe(true);
        if(fail || external!=(undo?2:1)){error="External state changed";return false;}
        if(throwing)throw std::runtime_error("Injected effect exception");
        held=true;external=undo?1:2;return true;
    }
    void End() noexcept override{++ends;held=false;if(observe)observe(false);}
};
}
int main(){try {
    std::string error;World world;const auto root=world.CreateEntity("Before"),child=world.CreateEntity("Child",root);
    EntitySelection selection;Require(selection.Assign(world,{root,child},child,error),error.c_str());
    const auto before=world.Capture();const auto beforeSelection=selection.Capture();
    world.Find(root)->name="After";selection.Replace(root);const auto after=world.Capture();const auto afterSelection=selection.Capture();
    int external=2;auto effect=std::make_shared<Effect>(external,4096);EntityHistory history;
    Require(history.Record(before,world,beforeSelection,afterSelection,error,effect),error.c_str());
    const auto bytes=history.StoredBytes();Require(bytes>=4096 && history.UndoCount()==0 && history.CanUndo(),"Effect admission/pending cost incorrect");
    effect->observe=[&](bool begin){if(begin){Require(world.Capture().dump()==after.dump() && history.UndoCount()==0 && history.StoredBytes()==bytes,"Replay mutated live history/world before external Begin");}else if(external==1){Require(world.Capture().dump()==before.dump() && selection.Capture()==beforeSelection && history.CanRedo(),"Ownership released before World/selection/history publication");}};
    effect->fail=true;Require(!history.Undo(world,selection,error) && world.Capture().dump()==after.dump() && selection.Capture()==afterSelection && external==2 && history.StoredBytes()==bytes && history.UndoCount()==0 && !history.CanRedo(),"Failed effect committed pending history or changed live state");
    effect->fail=false;effect->throwing=true;Require(!history.Undo(world,selection,error) && history.UndoCount()==0 && external==2 && effect->ends==2,"Throwing effect leaked ownership or changed pending state");effect->throwing=false;
    Require(history.Undo(world,selection,error) && external==1 && selection.Capture()==beforeSelection && !effect->held && effect->ends==3,error.c_str());
    effect->observe={};external=7;const auto redoBytes=history.StoredBytes();Require(!history.Redo(world,selection,error) && external==7 && history.CanRedo() && history.StoredBytes()==redoBytes && world.Capture().dump()==before.dump(),"Failed Redo advanced history/Scene");external=1;
    Require(history.Redo(world,selection,error) && external==2 && world.Capture().dump()==after.dump() && selection.Capture()==afterSelection,error.c_str());
    world.Find(root)->active=false;const auto calls=effect->begins;Require(!history.Undo(world,selection,error) && effect->begins==calls,"World mismatch invoked external effect");world.Find(root)->active=true;
    EntityHistory sourceOnly;auto only=std::make_shared<Effect>(external,64);Require(sourceOnly.Record(after,world,afterSelection,afterSelection,error,only),error.c_str());sourceOnly.Commit();Require(sourceOnly.UndoCount()==1 && sourceOnly.Undo(world,selection,error) && external==1 && world.Capture().dump()==after.dump(),"External-only command was lost as Scene no-op");Require(sourceOnly.Redo(world,selection,error) && external==2,error.c_str());
    EntityHistory budget(100,32);auto oversized=std::make_shared<Effect>(external,(std::numeric_limits<size_t>::max)());Require(!budget.Record(before,world,beforeSelection,afterSelection,error,oversized) && budget.StoredBytes()==0 && oversized->begins==0,"Effect budget overflow admitted/executed command");
    World chain;const auto id=chain.CreateEntity("One");EntitySelection chainSelection;chainSelection.Replace(id);EntityHistory merged;const auto s=chainSelection.Capture();int chainExternal=2;auto boundary=std::make_shared<Effect>(chainExternal,128);
    const auto one=chain.Capture();chain.Find(id)->name="Two";Require(merged.Record(one,chain,s,s,error),error.c_str());const auto two=chain.Capture();chain.Find(id)->name="Three";Require(merged.Record(two,chain,s,s,error,boundary),error.c_str());const auto three=chain.Capture();chain.Find(id)->name="Four";Require(merged.Record(three,chain,s,s,error),error.c_str());merged.Commit();
    Require(merged.UndoCount()==3 && merged.Undo(chain,chainSelection,error) && chain.Find(id)->name=="Three" && boundary->begins==0,"Plain edit merged across effect boundary");Require(merged.Undo(chain,chainSelection,error) && chain.Find(id)->name=="Two" && chainExternal==1,error.c_str());Require(merged.Undo(chain,chainSelection,error) && chain.Find(id)->name=="One" && boundary->begins==1,error.c_str());
    Require(merged.Redo(chain,chainSelection,error) && merged.Redo(chain,chainSelection,error) && merged.Redo(chain,chainSelection,error) && chain.Find(id)->name=="Four" && chainExternal==2,error.c_str());
    EntityHistory copied=sourceOnly;const auto copyCount=copied.UndoCount();EntityHistory swapped;swapped.Swap(copied);Require(swapped.UndoCount()==copyCount && copied.StoredBytes()==0,"No-throw history swap lost state");
    std::cout<<"PASS: history external-effect admission/cost, source-only entries, preallocated World/selection/history replay, ownership until publication, failure/exception/stale isolation and merge boundaries (not a Prefab file effect or Apply UI)\n";return 0;
}catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<"\n";return 1;}}
