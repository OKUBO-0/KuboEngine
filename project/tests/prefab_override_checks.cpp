#include "engine/editor/PrefabOverrides.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){
    using namespace Engine::Editor;using namespace Engine::Scene;
    try {
        EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Deleted",1);
        source.Find(2)->hasCamera=true;
        source.Find(2)->hasScript=true;source.Find(2)->script.assetPath="Scripts/check.lua";
        source.Find(2)->script.properties["a~/b"]=1.0;source.Find(2)->script.properties["sentinel"]=true;
        PrefabValue definition;std::string error;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
        EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory history;EntityId root=0;
        Require(InstantiatePrefab(world,definition,"0123456789abcdef0123456789abcdef",1,selection,history,root,error),error.c_str());
        PrefabOverrideReport report;Require(InspectPrefabOverrides(world,root,report,error) && report.entries.empty(),"Fresh instance/Scene IDs/external parent reported as overrides");
        const auto child=root+1,deleted=root+2;
        world.Find(root)->name="日本語 %s ###";world.Find(root)->transform.position={2,3,4};world.Find(child)->hasCamera=false;
        world.Find(child)->script.properties.erase("a~/b");world.Find(child)->script.properties["new~key"]=3.0;
        Require(world.DestroyEntity(deleted),"Deleted fixture failed");const auto added=world.CreateEntity("Added",child);
        const auto before=world.Capture().dump();const auto selected=selection.Capture();const auto undo=history.UndoCount();
        Require(InspectPrefabOverrides(world,root,report,error),error.c_str());
        Require(report.root==root && report.sourceGuid=="0123456789abcdef0123456789abcdef" && report.entries.size()==7,"Override report count/source/root mismatch");
        bool name=false,position=false,camera=false,removed=false,addition=false,fieldRemoved=false,fieldAdded=false;
        for(const auto& entry:report.entries) {
            if(entry.kind==PrefabOverrideKind::InstanceName){name=entry.local==1 && entry.scene==root && entry.path=="/name" && entry.instance==std::optional<std::string>("\"日本語 %s ###\"");}
            if(entry.path=="/transform/position")position=entry.kind==PrefabOverrideKind::Field && entry.local==1 && entry.baseline && entry.instance && entry.instance->find('2')!=std::string::npos;
            if(entry.path=="/camera/enabled")camera=entry.local==2 && entry.scene==child && entry.baseline==std::optional<std::string>("true") && entry.instance==std::optional<std::string>("false");
            if(entry.path=="/script/properties/a~0~1b")fieldRemoved=entry.baseline.has_value() && !entry.instance;
            if(entry.path=="/script/properties/new~0key")fieldAdded=!entry.baseline && entry.instance.has_value();
            if(entry.kind==PrefabOverrideKind::EntityRemoved)removed=entry.local==3 && entry.scene==deleted && entry.baseline.has_value() && !entry.instance;
            if(entry.kind==PrefabOverrideKind::EntityAdded)addition=entry.local==4 && entry.scene==added && !entry.baseline && entry.instance.has_value();
        }
        Require(name && position && camera && removed && addition && fieldRemoved && fieldAdded,"Missing name/atomic TRS/component flags/stable tombstone/addition/absent field details");
        Require(world.Capture().dump()==before && selection.Capture()==selected && history.UndoCount()==undo,"Inspection mutated World/selection/history");
        auto reordered=world.Capture();std::reverse(reordered["entities"].get<crude_json::array>().begin(),reordered["entities"].get<crude_json::array>().end());EntityWorld shuffled;
        Require(shuffled.Restore(reordered,error),error.c_str());PrefabOverrideReport other;Require(InspectPrefabOverrides(shuffled,root,other,error),error.c_str());
        Require(other.entries.size()==report.entries.size(),"Reorder changed count");
        for(size_t i=0;i<report.entries.size();++i){const auto& a=report.entries[i];const auto& b=other.entries[i];Require(a.kind==b.kind && a.local==b.local && a.scene==b.scene && a.path==b.path && a.baseline==b.baseline && a.instance==b.instance,"Reorder changed stable report");}
        const auto heldGuid=report.sourceGuid;const auto heldSize=report.entries.size();
        Require(!InspectPrefabOverrides(world,1,report,error) && report.sourceGuid==heldGuid && report.entries.size()==heldSize,"Nonlinked root replaced output");
        auto invalid=world;invalid.Find(root)->prefabLink["mapping"][size_t(1)]["entity"]=1.0;
        Require(!InspectPrefabOverrides(invalid,root,report,error) && report.sourceGuid==heldGuid && report.entries.size()==heldSize,"Invalid mapping replaced output");
        Require(PrefabOverridePointerToken("a~/b")=="a~0~1b","JSON Pointer token escaping ambiguous");
        Require(PrefabOverrideDisplayPrefix("日本語",2)==0 && PrefabOverrideDisplayPrefix("日本語",4)==3 && PrefabOverrideDisplayPrefix("日本語",9)==9 && PrefabOverrideDisplayPrefix("a日本",2)==1,"Display prefix split a UTF-8 code point");
        auto baselineChanged=world;baselineChanged.Find(root)->prefabLink["base"]["entities"][size_t(0)]["name"]="日本語 %s ###";
        Require(InspectPrefabOverrides(baselineChanged,root,other,error) && other.entries.size()==heldSize-1,"Inspection ignored persisted baseline/name ownership");
        std::cout<<"PASS: read-only Prefab baseline override report, stable IDs/tombstones/additions, instance names, atomic TRS, component removal, ordering, pointer escaping and rejected output isolation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
