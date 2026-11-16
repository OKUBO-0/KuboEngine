#include "engine/editor/EntitySelection.h"
#include "engine/editor/EntityGridAlignment.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main() {
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto a=world.CreateEntity("A"),b=world.CreateEntity("B",a),c=world.CreateEntity("C"),d=world.CreateEntity("D",b);
        EntitySelection selection;std::string error;
        Require(selection.Click(world,a,false,false,error) && selection.Primary()==a && selection.Ids().size()==1,"Replace selection failed");
        Require(selection.Click(world,c,true,false,error) && selection.Primary()==c && selection.Contains(a),"Ctrl additive failed");
        Require(selection.Click(world,c,true,false,error) && selection.Primary()==a && !selection.Contains(c),"Ctrl toggle removal failed");
        Require(selection.Click(world,d,false,true,error) && selection.Ids().size()==2 && selection.Contains(c) && selection.Primary()==d,"Shift forward range failed");
        Require(selection.Click(world,a,true,true,error) && selection.Ids().size()==4 && selection.Primary()==a,"Ctrl Shift reverse union failed");
        const auto stable=selection.Ids();Require(!selection.Click(world,999,false,false,error) && selection.Ids()==stable,"Invalid click mutated selection");
        Require(selection.Click(world,0,true,false,error) && selection.Ids()==stable,"Ctrl empty click cleared selection");
        EntitySelection visible;
        const EntityId rows[] = {a,d,c};
        Require(visible.Click(world,a,false,false,error) && visible.Click(world,c,false,true,error,rows) &&
            visible.Contains(d) && !visible.Contains(b) && visible.Ids().size()==3,"Range included a hidden row or ignored the displayed order");
        visible.Replace(b);
        Require(visible.Click(world,d,false,true,error,rows) && visible.Ids()==std::vector<EntityId>{d},"Hidden range anchor did not start a new range");
        EntityId primary=selection.Primary();world.DestroyEntity(c);selection.Synchronize(world,primary);Require(!selection.Contains(c) && primary==a,"Removed member not pruned");
        primary=b;selection.Synchronize(world,primary);Require(selection.Ids()==std::vector<EntityId>{b},"External primary did not replace multi-selection");
        world.DestroyEntity(b);selection.Synchronize(world,primary);Require(primary==0 && selection.Ids().empty(),"Removed primary not reconciled");
        world.Clear();const auto parent=world.CreateEntity("Parent"),child=world.CreateEntity("Child",parent),other=world.CreateEntity("Other");
        world.Find(parent)->transform={{0.25f,-0.25f,0.74f},{0.1f,0.2f,0.3f},{-2,3,0.5f}};
        world.Find(child)->transform={{0.21f,0.31f,0.41f},{13,0.2f,0.3f},{1,-2,3}};
        world.Find(other)->transform.position={-0.75f,1.26f,0.24f};
        const auto before=world.Capture().dump();Matrix4x4 targets[3]{};const std::vector<EntityId> ids{child,other,parent};
        for(size_t index=0;index<ids.size();++index)world.GetWorldMatrix(ids[index],targets[index]);
        const auto originalChild=world.Find(child)->transform;
        EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");
        Require(AlignEntitiesToWorldGrid(world,ids,0.5f,history,child,error),error.c_str());document.Observe(world);
        Require(document.IsDirty() && history.UndoCount()==1,"Batch alignment did not record one dirty command");
        for(size_t index=0;index<ids.size();++index) {Matrix4x4 actual{};world.GetWorldMatrix(ids[index],actual);for(int axis=0;axis<3;++axis)
            Require(std::abs(actual.m[3][axis]-std::round(targets[index].m[3][axis]/0.5f)*0.5f)<0.00005f,"Parent/child selection target depends on input order");}
        Require(world.Find(child)->transform.rotation.x==originalChild.rotation.x && world.Find(child)->transform.scale.y==originalChild.scale.y,"Batch altered authored winding/negative scale");
        const auto after=world.Capture().dump();primary=child;
        Require(history.Undo(world,primary,error) && world.Capture().dump()==before && primary==child,"Batch undo incomplete");document.Observe(world);Require(!document.IsDirty(),"Batch undo did not restore baseline");
        Require(history.Redo(world,primary,error) && world.Capture().dump()==after,"Batch redo incomplete");
        auto invalid=world;invalid.Find(other)->transform.scale.x=0;const auto rejected=invalid.Capture().dump();EntityHistory rejectedHistory;
        Require(!AlignEntitiesToWorldGrid(invalid,ids,1,rejectedHistory,child,error) && invalid.Capture().dump()==rejected && !rejectedHistory.CanUndo(),"Later invalid target partially committed earlier targets");
        Require(!AlignEntitiesToWorldGrid(world,{parent,parent},1,history,parent,error) && world.Capture().dump()==after,"Duplicate selection accepted/mutated world");
        Require(!AlignEntitiesToWorldGrid(world,{},1,history,parent,error) && !AlignEntitiesToWorldGrid(world,ids,1,history,999,error),"Empty/missing primary accepted");
        EntityHistory tiny(100,1);Require(!AlignEntitiesToWorldGrid(world,ids,1,tiny,child,error) && world.Capture().dump()==after && !tiny.CanUndo(),"Batch history rejection partially committed world");
        EntityWorld wide;for(int index=0;index<1024;++index)wide.CreateEntity("Wide");
        EntitySelection wideSelection;Require(wideSelection.Click(wide,1,false,false,error) && wideSelection.Click(wide,1024,false,true,error) && wideSelection.Ids().size()==1024,"Wide range selection incomplete");
        for(EntityId id=1;id<=1024;++id)Require(wideSelection.Contains(id),"Wide range membership index diverged");
        wide.DestroyEntity(512);primary=wideSelection.Primary();wideSelection.Synchronize(wide,primary);
        Require(wideSelection.Ids().size()==1023 && !wideSelection.Contains(512) && primary==1024,"Wide range pruning membership index diverged");
        Require(!AlignEntitiesToWorldGrid(world,std::vector<EntityId>(10001,parent),1,history,parent,error) && world.Capture().dump()==after,"Oversized batch mutated world");
        std::cout<<"PASS: transient Ctrl/Shift/range selection, pruning/external primary, original-world parent-first batch grid, one Undo/Redo/dirty and all-or-nothing failures\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
