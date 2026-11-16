#include "engine/editor/EntityBatchDelete.h"
#include "engine/editor/EntityBatchDuplicate.h"
#include "engine/editor/EntityGridAlignment.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;for(int i=0;i<5;++i)world.CreateEntity("Entity");
        EntitySelection selection;std::string error;
        Require(selection.Click(world,1,false,false,error) && selection.Click(world,3,false,true,error),error.c_str());
        const auto original=selection.Capture();Require(original.anchor==1 && original.primary==3,"Range anchor setup failed");
        const auto before=world.Capture();world.Find(3)->name="Edited";EntityHistory history;
        Require(history.Record(before,world,original,original,error),error.c_str());history.Commit();
        selection.Replace(5);
        Require(history.Undo(world,selection,error) && selection.Capture()==original && world.Capture().dump()==before.dump(),"Undo lost ordered multi-selection/primary/anchor");
        Require(selection.Click(world,5,false,true,error) && selection.Ids().size()==5,"Restored Shift anchor changed range");
        Require(history.Redo(world,selection,error) && selection.Capture()==original,"Redo lost selection despite later clicks");
        const auto unchanged=world.Capture();EntitySelection alternate;alternate.Replace(4);history.Clear();
        Require(history.Record(unchanged,world,original,alternate.Capture(),error) && !history.CanUndo(),"Selection-only change created authored history");
        auto invalid=original;invalid.ids.push_back(1);world.Find(3)->name="Changed";
        Require(!history.Record(unchanged,world,invalid,original,error) && !history.CanUndo(),"Invalid duplicate snapshot recorded");
        invalid=original;invalid.anchor=999;
        Require(!history.Record(unchanged,world,original,invalid,error) && !history.CanUndo(),"Missing anchor recorded");
        const auto held=selection.Capture();Require(!selection.Restore(world,invalid,error) && selection.Capture()==held,"Invalid snapshot partially published selection");
        history.Clear();Require(DuplicateSelectedSubtrees(world,selection,history,error),error.c_str());const auto clones=selection.Capture();
        Require(history.Undo(world,selection,error) && selection.Capture()==original,"Duplicate undo lost source selection");
        Require(history.Redo(world,selection,error) && selection.Capture()==clones,"Duplicate redo lost clone selection");
        history.Clear();const auto deleteBefore=world.Capture().dump();
        Require(DeleteSelectedSubtrees(world,selection,history,error) && selection.Ids().empty(),error.c_str());
        Require(history.Undo(world,selection,error) && selection.Capture()==clones && world.Capture().dump()==deleteBefore,"Delete undo lost forest selection");
        Require(history.Redo(world,selection,error) && selection.Ids().empty() && selection.Primary()==0,"Delete redo retained selection");
        Require(history.Undo(world,selection,error),error.c_str());history.Clear();
        world.Find(selection.Primary())->transform.position.x=0.3f;
        const auto gridSelection=selection.Capture();Require(AlignEntitiesToWorldGrid(world,selection.Ids(),0.5f,history,selection.Primary(),error,&gridSelection),error.c_str());
        selection.Replace(1);Require(history.Undo(world,selection,error) && selection.Capture()==gridSelection,"Grid undo lost full selection");
        Require(history.Redo(world,selection,error) && selection.Capture()==gridSelection,"Grid redo lost full selection");
        const auto conflictSelection=selection.Capture();world.Find(1)->name="External";
        Require(!history.Undo(world,selection,error) && selection.Capture()==conflictSelection && history.UndoCount()==1,"Conflict changed selection/history");
        EntityHistory bytes(100,before.dump().size()+world.Capture().dump().size());
        Require(!bytes.Record(before,world,original,original,error) && !bytes.CanUndo(),"Selection storage omitted from memory budget");
        // Anchor can be live but deselected, including an empty Ctrl-toggled selection.
        EntitySelection empty;empty.Replace(2);Require(empty.Click(world,2,true,false,error),error.c_str());const auto emptyState=empty.Capture();
        Require(emptyState.ids.empty() && emptyState.anchor==2 && empty.Restore(world,emptyState,error),"Empty live range anchor rejected");
        EntityWorld mergeWorld;mergeWorld.CreateEntity("One");mergeWorld.CreateEntity("Two");
        EntitySelection mergeSelection;Require(mergeSelection.Click(mergeWorld,1,false,false,error) && mergeSelection.Click(mergeWorld,2,true,false,error),error.c_str());
        const auto mergeState=mergeSelection.Capture();const auto mergeStart=mergeWorld.Capture();EntityHistory merged;
        mergeWorld.Find(1)->name="Frame one";Require(merged.Record(mergeStart,mergeWorld,mergeState,mergeState,error),error.c_str());
        const auto mergeMid=mergeWorld.Capture();mergeWorld.Find(1)->name="Frame two";
        Require(merged.Record(mergeMid,mergeWorld,mergeState,mergeState,error),error.c_str());merged.Commit();
        Require(merged.UndoCount()==1 && merged.Undo(mergeWorld,mergeSelection,error) && mergeWorld.Capture().dump()==mergeStart.dump() && mergeSelection.Capture()==mergeState,"Continuous edit lost grouped full selection");
        merged.Clear();const auto splitStart=mergeWorld.Capture();mergeWorld.Find(1)->name="First command";
        Require(merged.Record(splitStart,mergeWorld,mergeState,mergeState,error),error.c_str());
        const auto splitMid=mergeWorld.Capture();mergeSelection.Replace(1);const auto singleState=mergeSelection.Capture();mergeWorld.Find(1)->name="Second command";
        Require(merged.Record(splitMid,mergeWorld,singleState,singleState,error),error.c_str());merged.Commit();
        Require(merged.UndoCount()==2 && merged.Undo(mergeWorld,mergeSelection,error) && mergeSelection.Capture()==singleState && merged.Undo(mergeWorld,mergeSelection,error) && mergeSelection.Capture()==mergeState,"Different selection chains merged across commands");
        const auto safeState=mergeSelection.Capture();auto oversized=safeState;oversized.ids.resize(10001,1);
        Require(!mergeSelection.Restore(mergeWorld,oversized,error) && mergeSelection.Capture()==safeState,"Oversized selection snapshot accepted/published");
        auto wrongPrimary=safeState;wrongPrimary.primary=999;
        Require(!mergeSelection.Restore(mergeWorld,wrongPrimary,error) && mergeSelection.Capture()==safeState,"Missing primary snapshot accepted/published");
        std::cout<<"PASS: ordered full-selection/primary/Shift-anchor history, batch duplicate/delete/grid, selection-only no-op, snapshot/budget/conflict isolation and empty-anchor restoration\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
