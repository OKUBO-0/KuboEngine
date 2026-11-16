#pragma once
#include <cstddef>
#include <string>

namespace Engine::Editor {
// An authored command may own a non-Scene effect, such as a Prefab source swap.
// Begin must validate expected external state before changing it. On success it
// keeps ownership through Scene/selection/history and optional prepared document
// dirty publication; End releases it. Effects must not re-enter or modify the
// live world, selection, history or document during Begin/End.
// Failed Begin must preserve external state (or retain a diagnosed recovery
// transaction), never overwrite an unrelated writer. Calls are non-reentrant
// and serialized by the Editor's authoring owner, not a background worker.
class EntityHistoryEffect {
public:
    explicit EntityHistoryEffect(size_t retainedBytes):retainedBytes_(retainedBytes){}
    virtual ~EntityHistoryEffect()=default;
    size_t Bytes() const noexcept{return retainedBytes_;}
    virtual bool Begin(bool undo,std::string& error)=0;
    virtual void End() noexcept=0;
private:
    // Fixed admission cost: late lease acquisition must not increase retained
    // history payload. Implementations include/reserve paths and byte snapshots.
    const size_t retainedBytes_;
};
}
