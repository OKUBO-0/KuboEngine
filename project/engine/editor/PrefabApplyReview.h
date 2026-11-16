#pragma once
#include "PrefabThreeWay.h"
#include "PrefabFieldRevert.h" // Escaped pointer decoding and read-only value lookup.

namespace Engine::Editor {
struct PrefabApplySourceReplacement {
    PrefabThreeWayEntry comparison;
    std::optional<std::string> proposed;
};
struct PrefabApplySourceReview {
    PrefabThreeWayReport comparison;
    std::vector<PrefabApplySourceReplacement> replacements;
    bool RequiresApproval() const noexcept {return !replacements.empty();}
};
// Read-only review of the actual proposed source, not a second merge policy.
// Caller supplies the prepared Apply plan and owns its native source revision.
inline bool BuildPrefabApplySourceReview(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& currentSource,const std::string& guid,const PrefabValue& proposedSource,
    PrefabApplySourceReview& output,std::string& error) {
    static_assert(std::is_nothrow_move_assignable_v<PrefabApplySourceReview>);
    try {
        PrefabApplySourceReview candidate;
        if(!InspectPrefabThreeWay(world,root,currentSource,guid,candidate.comparison,error))return false;
        PrefabValue proposed;if(!ValidatePrefabDocument(proposedSource,proposed,error))return false;
        if(proposed["root"]!=currentSource["root"]){error="Apply review proposed root identity changed";return false;}
        std::map<Scene::EntityId,const PrefabValue*> nodes;
        for(const auto& node:proposed["world"]["entities"].get<crude_json::array>())nodes.emplace(Scene::EntityId(node["id"].get<crude_json::number>()),&node);
        size_t text=0;constexpr size_t TextBudget=3*Scene::SceneDocument::MaxBytes;
        for(const auto& entry:candidate.comparison.entries) {
            using C=PrefabThreeWayChange;
            if(entry.change!=C::SourceOnly && entry.change!=C::Conflict)continue;
            const PrefabValue* value=nodes.contains(entry.local)?nodes.at(entry.local):nullptr;
            if(value && entry.kind!=PrefabThreeWayKind::EntityPresence){
                std::vector<std::string> tokens;if(!PrefabOverrideTokens(entry.path,tokens,error))return false;
                value=PrefabOverrideValue(*value,tokens);
            }
            std::optional<std::string> next;if(value)next=value->dump();
            if(next==entry.source)continue; // Incoming change is retained by the plan.
            const auto bytes=next?next->size():0;
            if(bytes>TextBudget-text){error="Apply review proposed text budget exceeded";return false;}text+=bytes;
            candidate.replacements.push_back({entry,std::move(next)});
        }
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
