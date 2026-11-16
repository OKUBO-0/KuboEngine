#include "engine/editor/PrefabApplyReview.h"
#include "engine/editor/PrefabApplyPlan.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::Scene;
    const std::string guid="0123456789abcdef0123456789abcdef";std::string error;
    EntityWorld authored;authored.CreateEntity("Root");PrefabValue definition;Require(CapturePrefabSubtree(authored,1,definition,error),error.c_str());
    EntityWorld world;EntitySelection selection;EntityHistory history;EntityId root=0;
    Require(InstantiatePrefab(world,definition,guid,0,selection,history,root,error),error.c_str());history.Clear();
    const auto before=world.Capture().dump();const auto selected=selection.Capture();const auto originalHistory=history;
    auto incoming=definition;incoming["world"]["entities"][size_t(0)]["transform"]["position"]=crude_json::array{3.0,0.0,0.0};
    PrefabApplyPlan plan;PrefabApplySourceReview review;
    Require(BuildPrefabApplyPlan(world,root,incoming,guid,plan,error) && BuildPrefabApplySourceReview(world,root,incoming,guid,plan.source,review,error) && !review.RequiresApproval(),"Preserved source-only field was marked for replacement");
    auto mine=world;mine.Find(root)->transform.position={2,0,0};
    Require(BuildPrefabApplyPlan(mine,root,incoming,guid,plan,error) && BuildPrefabApplySourceReview(mine,root,incoming,guid,plan.source,review,error) && review.replacements.size()==1 && review.replacements.front().comparison.path=="/transform/position" && review.replacements.front().comparison.change==PrefabThreeWayChange::Conflict && review.replacements.front().proposed==std::optional<std::string>("[2,0,0]"),"Actual conflicting source replacement missing");
    incoming=definition;incoming["world"]["entities"][size_t(0)]["name"]="External source rename";
    Require(BuildPrefabApplyPlan(world,root,incoming,guid,plan,error) && BuildPrefabApplySourceReview(world,root,incoming,guid,plan.source,review,error) && review.replacements.size()==1 && review.replacements.front().comparison.kind==PrefabThreeWayKind::InstanceName && review.replacements.front().comparison.change==PrefabThreeWayChange::SourceOnly,"Instance-owned name source replacement was omitted");
    const auto retained=review.replacements.front().comparison.source;
    auto invalid=plan.source;invalid["format"]="invalid";
    Require(!BuildPrefabApplySourceReview(world,root,incoming,guid,invalid,review,error) && review.replacements.size()==1 && review.replacements.front().comparison.source==retained,"Invalid proposed source replaced prior output");
    Require(!BuildPrefabApplySourceReview(world,root,incoming,"wrong-guid",plan.source,review,error) && review.RequiresApproval(),"Wrong GUID accepted or previous review lost");
    EntityWorld tree;tree.CreateEntity("Tree");tree.CreateEntity("Child",1);PrefabValue treeDefinition;Require(CapturePrefabSubtree(tree,1,treeDefinition,error),error.c_str());
    EntityWorld treeWorld;EntitySelection treeSelection;EntityHistory treeHistory;EntityId treeRoot=0;
    Require(InstantiatePrefab(treeWorld,treeDefinition,guid,0,treeSelection,treeHistory,treeRoot,error),error.c_str());treeWorld.Find(treeRoot+1)->transform.position={9,0,0};
    auto deleted=treeDefinition;deleted["world"]["entities"].get<crude_json::array>().pop_back();PrefabApplySourceReview structural;
    Require(BuildPrefabApplyPlan(treeWorld,treeRoot,deleted,guid,plan,error) && BuildPrefabApplySourceReview(treeWorld,treeRoot,deleted,guid,plan.source,structural,error) && structural.replacements.size()==1 && structural.replacements.front().comparison.kind==PrefabThreeWayKind::EntityPresence && !structural.replacements.front().comparison.source && structural.replacements.front().proposed.has_value(),"Source deletion overwritten by modified child omitted structural replacement");
    Require(world.Capture().dump()==before && selection.Capture()==selected && history.HasSameState(originalHistory),"Review mutated World/selection/history");
    std::cout<<"PASS: actual-plan source replacement review, preserved source-only field, conflicting atomic TRS, instance-owned source-only rename, invalid/GUID output isolation and readonly authoring\n";return 0;
}catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
