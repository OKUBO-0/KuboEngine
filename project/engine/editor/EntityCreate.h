#pragma once
#include "EntityHistory.h"

namespace Engine::Editor {
inline bool CreateAuthoredEntity(Scene::EntityWorld& world,const std::string& name,
    Scene::EntityId parent,EntitySelection& selection,EntityHistory& history,
    Scene::EntityId& created,std::string& error)
{
    const auto before=world.Capture();Scene::EntityWorld checked;
    EntitySelection checkedSelection;
    if(!checked.Restore(before,error) || !checkedSelection.Restore(checked,selection.Capture(),error))return false;
    if(parent && !checked.Find(parent)){error="Entity creation parent does not exist";return false;}
    auto candidate=world;const auto id=candidate.CreateEntity(name,parent);
    if(!id){error="Entity creation exhausted JSON-exact IDs or invalid allocation data";return false;}
    if(candidate.Capture().dump().size()>Scene::SceneDocument::MaxBytes){error="Entity creation exceeds Scene document budget";return false;}
    EntitySelection next;next.Replace(id);
    history.Commit();if(!history.Record(before,candidate,selection.Capture(),next.Capture(),error))return false;
    history.Commit();world=std::move(candidate);selection=std::move(next);created=id;error.clear();return true;
}
}
