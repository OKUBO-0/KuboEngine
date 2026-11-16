#include "engine/editor/PrefabApplyPlan.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::Scene;
    const std::string guid="0123456789abcdef0123456789abcdef";std::string error;
    EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Deleted",1);
    PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
    EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;selection.Replace(1);EntityHistory history;EntityId chosen=0,other=0;
    Require(InstantiatePrefab(world,definition,guid,1,selection,history,chosen,error) && chosen==2,error.c_str());
    Require(InstantiatePrefab(world,definition,guid,1,selection,history,other,error) && other==5,error.c_str());
    PrefabApplyPlan unchanged;
    Require(BuildPrefabApplyPlan(world,chosen,definition,guid,unchanged,error) && unchanged.source.dump()==definition.dump() && unchanged.world.Capture().dump()==world.Capture().dump() && unchanged.world.AllocationHighWatermark()==world.AllocationHighWatermark(),"Unchanged Apply plan was not exact no-op");
    auto unsorted=definition;std::swap(unsorted["world"]["entities"][size_t(1)],unsorted["world"]["entities"][size_t(2)]);
    EntityWorld ordered;EntitySelection os;EntityHistory oh;EntityId orderedRoot=0;
    Require(InstantiatePrefab(ordered,unsorted,guid,0,os,oh,orderedRoot,error) && BuildPrefabApplyPlan(ordered,orderedRoot,unsorted,guid,unchanged,error) && unchanged.source.dump()==unsorted.dump() && unchanged.world.Capture().dump()==ordered.Capture().dump(),"Non-sorted source storage changed during unchanged Apply");
    world.Find(chosen)->name="Apply name";world.Find(3)->transform.position.x=7;
    Require(world.DestroyEntity(4),"Chosen deletion failed");const auto electedExtra=world.CreateEntity("Elected addition",chosen);
    world.Find(other)->name="Other name";world.Find(6)->active=false;
    Require(world.DestroyEntity(7),"Other deletion failed");const auto otherExtra=world.CreateEntity("Other addition",other);
    const auto before=world.Capture().dump();const auto counter=world.AllocationHighWatermark();const auto selected=selection.Capture();const auto undo=history.UndoCount();
    PrefabApplyPlan plan;Require(BuildPrefabApplyPlan(world,chosen,definition,guid,plan,error),error.c_str());
    EntityWorld appliedSource;Require(appliedSource.Restore(plan.source["world"],error),error.c_str());
    Require(appliedSource.Find(1)->name=="Apply name" && appliedSource.Find(2)->transform.position.x==7 && !appliedSource.Find(3) && appliedSource.Find(4)->name=="Elected addition","Apply source lost chosen overrides/deletion/addition");
    Require(plan.instances.size()==2 && plan.instances[0].root==chosen && plan.instances[1].root==other,"All-instance plan order wrong");
    Require(plan.world.Find(chosen)->parent==1 && plan.world.Find(electedExtra)->name=="Elected addition" && plan.world.Find(electedExtra)->parent==chosen,"Elected addition Scene ID or external parent changed");
    Require(plan.world.Find(other)->name=="Other name" && plan.world.Find(6)->transform.position.x==7 && !plan.world.Find(6)->active,"Other instance overrides/source inheritance lost");
    Require(plan.world.Find(otherExtra)->name=="Other addition" && !plan.world.Find(7),"Other addition/deletion lost");
    const auto& otherMap=plan.world.Find(other)->prefabLink["mapping"].get<crude_json::array>();EntityId inheritedExtra=0;
    for(const auto& pair:otherMap)if(pair["local"].get<crude_json::number>()==4)inheritedExtra=EntityId(pair["entity"].get<crude_json::number>());
    Require(inheritedExtra && inheritedExtra!=otherExtra && plan.world.Find(inheritedExtra)->name=="Elected addition","Source/instance additions collided during propagation");
    Require(world.Capture().dump()==before && world.AllocationHighWatermark()==counter && selection.Capture()==selected && history.UndoCount()==undo,"Plan mutated live state");
    Require(plan.world.AllocationHighWatermark()>counter,"Propagation allocation counter not advanced");
    auto arrayOverride=world;arrayOverride.Find(6)->transform.position.y=9;PrefabApplyPlan arrayPlan;
    Require(BuildPrefabApplyPlan(arrayOverride,chosen,definition,guid,arrayPlan,error) && arrayPlan.world.Find(6)->transform.position.x==0 && arrayPlan.world.Find(6)->transform.position.y==9,"Atomic position-array override was incorrectly merged axis by axis");
    const auto held=plan.world.Capture().dump(),heldSource=plan.source.dump();const auto heldReports=plan.instances.size();
    Require(!BuildPrefabApplyPlan(world,chosen,definition,"ffffffffffffffffffffffffffffffff",plan,error) && plan.world.Capture().dump()==held && plan.source.dump()==heldSource && plan.instances.size()==heldReports,"Wrong GUID mutated outputs");
    auto changedRoot=definition;changedRoot["root"]=10.0;
    for(auto& node:changedRoot["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==1)node["id"]=10.0;if(node["parent"].get<crude_json::number>()==1)node["parent"]=10.0;}
    PrefabValue validated;Require(ValidatePrefabDocument(changedRoot,validated,error),"Changed-root fixture not independently valid");
    Require(!BuildPrefabApplyPlan(world,chosen,changedRoot,guid,plan,error) && plan.world.Capture().dump()==held && plan.source.dump()==heldSource,"Changed source root altered Apply outputs");
    auto highData=world.Capture();highData["entities"][size_t(0)]["id"]=double(EntityWorld::MaximumEntityId);
    for(auto& node:highData["entities"].get<crude_json::array>())if(node["parent"].get<crude_json::number>()==1)node["parent"]=double(EntityWorld::MaximumEntityId);
    EntityWorld exhausted;Require(exhausted.Restore(highData,error),error.c_str());const auto exhaustedBefore=exhausted.Capture().dump();
    Require(!BuildPrefabApplyPlan(exhausted,chosen,definition,guid,plan,error) && exhausted.Capture().dump()==exhaustedBefore && plan.world.Capture().dump()==held && plan.source.dump()==heldSource,"Propagation allocation exhaustion partially published");
    Require(exhausted.DestroyEntity(electedExtra) && exhausted.DestroyEntity(otherExtra),"Exhaustion positive fixture deletion failed");PrefabApplyPlan noAllocation;
    Require(BuildPrefabApplyPlan(exhausted,chosen,definition,guid,noAllocation,error) && noAllocation.world.AllocationHighWatermark()==EntityWorld::MaximumEntityId+1 && noAllocation.world.CreateEntity("Cannot recycle")==0,"No-allocation propagation lost exhausted high-water");
    // A source addition and elected addition must reserve distinct local IDs;
    // the frozen elected mapping is reused even after its addition enters source.
    auto incoming=definition;auto added=definition["world"]["entities"][size_t(1)];added["id"]=4.0;added["name"]="Incoming addition";incoming["world"]["entities"].push_back(added);
    Require(BuildPrefabApplyPlan(world,chosen,incoming,guid,plan,error),error.c_str());
    Require(appliedSource.Restore(plan.source["world"],error) && appliedSource.Find(4)->name=="Incoming addition" && appliedSource.Find(5)->name=="Elected addition" && plan.world.Find(electedExtra)->name=="Elected addition","Current source addition displaced elected Scene identity");
    // A later instance's orphan conflict must reject the WHOLE plan, including
    // elected source/candidate changes, not partially publish earlier instances.
    EntityWorld conflict;EntityId first=0,second=0;EntitySelection cs;EntityHistory ch;
    EntityWorld tree;tree.CreateEntity("Root");tree.CreateEntity("Parent",1);tree.CreateEntity("Leaf",2);PrefabValue treeDef;
    Require(CapturePrefabSubtree(tree,1,treeDef,error) && InstantiatePrefab(conflict,treeDef,guid,0,cs,ch,first,error) && InstantiatePrefab(conflict,treeDef,guid,0,cs,ch,second,error),error.c_str());
    Require(conflict.DestroyEntity(2),"Tree subtree deletion failed");conflict.Find(6)->transform.position.x=2;
    const auto conflictBefore=conflict.Capture().dump(),kept=plan.world.Capture().dump(),keptSource=plan.source.dump();
    Require(!BuildPrefabApplyPlan(conflict,first,treeDef,guid,plan,error) && plan.world.Capture().dump()==kept && plan.source.dump()==keptSource && conflict.Capture().dump()==conflictBefore,"Later instance structural conflict partially published plan");
    PrefabInstanceCapture capture;Require(CapturePrefabInstance(world,chosen,capture,error,&definition),error.c_str());EntityWorld candidate=world;
    auto stale=world;stale.Find(3)->name="Changed after capture";
    Require(!BuildPrefabMergedCandidate(stale,chosen,guid,capture,definition,capture.instance,candidate,error) && candidate.Capture().dump()==before,"Stale frozen capture overwritten live edit");
    auto invented=capture;invented.sceneByLocal.emplace(100,1);
    Require(!BuildPrefabMergedCandidate(world,chosen,guid,invented,definition,capture.instance,candidate,error) && candidate.Capture().dump()==before,"Invented capture mapping accepted");
    // Retain a source-deleted edited node as an unmapped instance extra.
    EntityWorld retention;EntityId retainedRoot=0;EntitySelection rs;EntityHistory rh;
    Require(InstantiatePrefab(retention,definition,guid,0,rs,rh,retainedRoot,error),error.c_str());retention.Find(3)->transform.position.x=2;
    auto removedSource=definition;removedSource["world"]["entities"].get<crude_json::array>().pop_back();
    PrefabInstanceCapture retainedCapture;PrefabValue merged;PrefabMergeReport report;
    Require(CapturePrefabInstance(retention,retainedRoot,retainedCapture,error,&removedSource) && MergePrefabDocuments(retainedCapture.baseline,removedSource,retainedCapture.instance,merged,report,error) && BuildPrefabMergedCandidate(retention,retainedRoot,guid,retainedCapture,removedSource,merged,candidate,error),error.c_str());
    Require(candidate.Find(3) && candidate.Find(3)->transform.position.x==2 && candidate.Find(retainedRoot)->prefabLink["mapping"].get<crude_json::array>().size()==2 && report.retainedRemoved==std::vector<EntityId>{3},"Edited source-deleted node lost or remained in baseline mapping");
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-prefab-apply-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto sceneFile=fixture/L"候補.scene";EntityWorld reloaded;
    Require(plan.world.Save(sceneFile,error) && reloaded.Load(sceneFile,error) && reloaded.Capture().dump()==plan.world.Capture().dump(),"Apply candidate baseline/mappings failed Unicode Scene roundtrip");
    std::filesystem::remove_all(fixture);
    std::cout<<"PASS: read-only all-instance Apply plan, frozen chosen identities, incoming source reservations, overrides/additions/deletions, retained removed nodes, late conflict atomicity and stale/invented capture isolation\n";return 0;
}catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
