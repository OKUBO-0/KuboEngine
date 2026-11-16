#include "ScriptRuntime.h"
#include "LuaScript.h"
#include "../physics/PhysicsWorld.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <exception>

namespace Engine::Scripting {
namespace {

enum class Hook { Create, Update, Destroy, FixedUpdate };
enum class Operation { Translate, SetVelocity, SetActive };
// Lua receives detached hook records so structural publication cannot invalidate
// its callback reference. Scalar-only hooks still need a no-throw live self
// commit; structural hooks already contain these values. Never resurrect self.
void CommitLuaSelf(Scene::EntityWorld& world,const Scene::EntityRecord& candidate) noexcept
{
    if(auto* live=world.Find(candidate.id)){
        live->transform=candidate.transform;live->active=candidate.active;
        if(live->hasRigidBody)live->rigidBody.velocity=candidate.rigidBody.velocity;
    }
}
struct ParsedOperation {
    Hook hook;
    Operation operation;
    Vector3 value{};
    bool boolean = true;
};

std::string BoundLoadText(const std::string& text,size_t limit)
{
    if(text.size()<=limit)return text;
    auto end=limit-3;
    while(end && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
    return text.substr(0,end)+"...";
}

bool ReadVector(std::istringstream& stream, Vector3& value)
{
    return static_cast<bool>(stream >> value.x >> value.y >> value.z) &&
        std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void Execute(const std::vector<ParsedOperation>& operations, Hook hook, Scene::EntityRecord& entity, float deltaSeconds)
{
    for (const auto& operation : operations) {
        if (operation.hook != hook) continue;
        if (operation.operation == Operation::Translate) entity.transform.position += operation.value * deltaSeconds;
        else if (operation.operation == Operation::SetVelocity && entity.hasRigidBody) entity.rigidBody.velocity = operation.value;
        else if (operation.operation == Operation::SetActive) entity.active = operation.boolean;
    }
}

} // namespace

std::string ScriptRuntime::DescribeLoadFailure(Scene::EntityId id,const std::string& reference,const std::string& error)
{
    return "Entity "+std::to_string(id)+" ["+BoundLoadText(reference,256)+"]: "+BoundLoadText(error,2048);
}

std::string ScriptRuntime::GetAssetLoadError(const std::string& key) const
{
    const auto found=failedLoads_.find(key);
    return found==failedLoads_.end()?std::string{}:found->second.error;
}

bool ScriptRuntime::ConfigureLuaResourceLimits(LuaResourceLimits limits,std::string& error)
{
    try{
        if(!scripts_.empty() || !instances_.empty() || luaBudget_->Usage().states || luaBudget_->FrameActive())throw std::runtime_error("Configure Lua limits before loading scripts, or after Shutdown and Clear");
        auto next=std::make_shared<LuaResourceBudget>(limits);
        luaBudget_=std::move(next);failedLoads_.clear();nextLuaStart_=nextFixedLuaStart_=0;error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

bool ScriptRuntime::Register(std::string assetPath, ScriptCallbacks callbacks)
{
    if (assetPath.empty() || (!callbacks.onUpdate && !callbacks.onFixedUpdate &&
        !callbacks.onTriggerEnter && !callbacks.onTriggerExit)) return false;
    failedLoads_.erase(assetPath);
    scripts_[std::move(assetPath)] = {[callbacks=std::move(callbacks)] { return callbacks; }, nextRevision_++};
    return true;
}

bool ScriptRuntime::LoadAsset(const std::string& assetPath, const std::filesystem::path& assetRoot, std::string& error, bool retryFailure)
{
    try {
        const auto relative = std::filesystem::path(std::u8string(assetPath.begin(), assetPath.end())).lexically_normal();
        if (relative.empty() || relative.has_root_name() || relative.has_root_directory()) throw std::runtime_error("Script asset path must be project-relative");
        if (assetPath.find(':') != std::string::npos || assetPath.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid script asset path");
        for (const auto& component : relative) if (component == "..") throw std::runtime_error("Script path escapes project");
        return LoadResolvedAsset(assetPath, assetRoot / relative, error, retryFailure);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool ScriptRuntime::LoadResolvedAsset(const std::string& assetPath, const std::filesystem::path& source, std::string& error, bool retryFailure)
{
    FailedLoad attempt;
    try {
        if (assetPath.empty()) throw std::runtime_error("Script asset path is empty");
        const auto size = std::filesystem::file_size(source);
        attempt.source = std::filesystem::absolute(source).lexically_normal();
        attempt.size = size;
        attempt.modified = std::filesystem::last_write_time(source);
        const auto previous = failedLoads_.find(assetPath);
        if (!retryFailure && previous != failedLoads_.end() && previous->second.source == attempt.source &&
            previous->second.size == size && previous->second.modified == attempt.modified) {
            error = previous->second.error;
            return false;
        }
        if (size > 1024 * 1024) throw std::runtime_error("Script exceeds 1 MiB");
        std::ifstream file(source, std::ios::binary);
        std::string bytes(static_cast<size_t>(size), '\0');
        if (!file.read(bytes.data(), static_cast<std::streamsize>(size)) || file.peek() != EOF)
            throw std::runtime_error("Cannot read script asset: " + assetPath);
        if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
        auto extension=source.extension().wstring();
        for(auto& character:extension) if(character>=L'A' && character<=L'Z') character+=L'a'-L'A';
        if (extension == L".lua") {
            ScriptProperties defaults;
            auto factory=CreateLuaScriptFactory(bytes,assetPath,inputFrame_,defaults,audioRequests_,luaBudget_,sceneView_,animatorRequests_,sceneTransitions_);
            scripts_[assetPath]={std::move(factory),nextRevision_++,std::move(defaults),true};
            failedLoads_.erase(assetPath);
            error.clear();return true;
        }
        std::istringstream input(bytes);
        if (!input) throw std::runtime_error("Cannot open script asset: " + assetPath);
        std::vector<ParsedOperation> operations;
        std::string line;
        size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto comment = line.find('#');
            if (comment != std::string::npos) line.resize(comment);
            std::istringstream stream(line);
            std::string hookName;
            std::string operationName;
            if (!(stream >> hookName)) continue;
            if (!(stream >> operationName)) throw std::runtime_error("Missing script operation at line " + std::to_string(lineNumber));
            Hook hook;
            if (hookName == "on_create") hook = Hook::Create;
            else if (hookName == "on_update") hook = Hook::Update;
            else if (hookName == "on_destroy") hook = Hook::Destroy;
            else if (hookName == "on_fixed_update") hook = Hook::FixedUpdate;
            else throw std::runtime_error("Unknown script hook at line " + std::to_string(lineNumber));
            ParsedOperation operation{hook, Operation::Translate};
            if (operationName == "translate") {
                if (!ReadVector(stream, operation.value)) throw std::runtime_error("translate requires x y z at line " + std::to_string(lineNumber));
            } else if (operationName == "set_velocity") {
                operation.operation = Operation::SetVelocity;
                if (!ReadVector(stream, operation.value)) throw std::runtime_error("set_velocity requires x y z at line " + std::to_string(lineNumber));
            } else if (operationName == "set_active") {
                operation.operation = Operation::SetActive;
                std::string value;
                if (!(stream >> value) || (value != "true" && value != "false")) throw std::runtime_error("set_active requires true or false at line " + std::to_string(lineNumber));
                operation.boolean = value == "true";
            } else throw std::runtime_error("Unknown script operation at line " + std::to_string(lineNumber));
            std::string trailing;
            if (stream >> trailing) throw std::runtime_error("Unexpected script arguments at line " + std::to_string(lineNumber));
            operations.push_back(operation);
        }
        ScriptCallbacks callbacks;
        callbacks.onCreate = [operations](Scene::EntityRecord& entity) { Execute(operations, Hook::Create, entity, 1.0f); };
        callbacks.onUpdate = [operations](Scene::EntityRecord& entity, float dt) { Execute(operations, Hook::Update, entity, dt); };
        callbacks.onDestroy = [operations](Scene::EntityRecord& entity) { Execute(operations, Hook::Destroy, entity, 1.0f); };
        if(std::any_of(operations.begin(),operations.end(),[](const auto& operation){return operation.hook==Hook::FixedUpdate;}))
            callbacks.onFixedUpdate=[operations](Scene::EntityRecord& entity,float dt){Execute(operations,Hook::FixedUpdate,entity,dt);};
        if (!Register(assetPath, std::move(callbacks))) throw std::runtime_error("Script asset registration failed");
        failedLoads_.erase(assetPath);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        // Keep only bounded diagnostics/metadata, not source bytes or a failed VM.
        // Resolution/identity checks remain outside this cache and run every time.
        if (!assetPath.empty() && assetPath.size()<=4096 && (failedLoads_.size() < 256 || failedLoads_.contains(assetPath))) {
            attempt.error = BoundLoadText(error,2048);
            failedLoads_[assetPath] = std::move(attempt);
        }
        return false;
    }
}

bool ScriptRuntime::LoadWorldAssets(Scene::EntityWorld& world, const std::filesystem::path& assetRoot, std::string& error)
{
    std::string firstError;size_t failures=0;
    for (const auto& entity : world.GetEntities()) {
        if (!entity.hasScript || !entity.script.enabled) continue;
        if (!entity.script.assetGuid.empty()) {
            Unregister(entity.script.RegistrationKey());
            if(!failures++)firstError=DescribeLoadFailure(entity.id,entity.script.RegistrationKey(),"GUID scripts require the project asset registry loader");
            continue;
        }
        if (entity.script.assetPath.empty() || Contains(entity.script.assetPath)) continue;
        std::string current;
        if (!LoadAsset(entity.script.assetPath, assetRoot, current, false)) {
            if(!failures++)firstError=DescribeLoadFailure(entity.id,entity.script.RegistrationKey(),current);
        }
    }
    error=std::move(firstError);
    if(failures>1)error+=" ("+std::to_string(failures)+" script load failures)";
    return failures==0;
}

void ScriptRuntime::Unregister(const std::string& assetPath)
{
    scripts_.erase(assetPath);
    failedLoads_.erase(assetPath);
}

void ScriptRuntime::Clear()
{
    sceneTransitions_->SetEnabled(false);
    audioRequests_->SetEnabled(false);
    animatorRequests_->SetEnabled(false);
    ResetInput();
    scripts_.clear();
    errors_.clear();
    failedLoads_.clear();
    nextLuaStart_=nextFixedLuaStart_=0;
}

void ScriptRuntime::Update(Scene::EntityWorld& world, float deltaSeconds)
{
    Dispatch(world,deltaSeconds,false);
}

void ScriptRuntime::FixedUpdate(Scene::EntityWorld& world,float fixedSeconds)
{
    if(shuttingDown_)return;
    if(!std::isfinite(fixedSeconds) || fixedSeconds<=0)return;
    // Swap the shared Lua input view for the entire dispatch, including Create /
    // Destroy. Restore variable input even if a native callback throws.
    auto fixedFrame=fixedInput_.Consume();
    struct InputScope {
        InputSystem::InputFrame& shared;InputSystem::InputFrame& saved;
        InputScope(InputSystem::InputFrame& sharedInput,InputSystem::InputFrame& tickInput) noexcept:shared(sharedInput),saved(tickInput){shared.Swap(saved);}
        ~InputScope(){shared.Swap(saved);}
    } inputScope(*inputFrame_,fixedFrame);
    Dispatch(world,fixedSeconds,true);
}

void ScriptRuntime::Dispatch(Scene::EntityWorld& world,float deltaSeconds,bool fixed)
{
    if(shuttingDown_)return;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) return;
    ScriptSceneView::WriteScope sceneScope(*sceneView_,world,assetOwner_.get(),animatorReader_);
    LuaResourceBudget::FrameScope frame(*luaBudget_);
    std::vector<Scene::EntityId> retiring;
    for (const auto& [id, instance] : instances_) {
        const auto* entity = world.Find(id);
        if (!CanRun(entity) || !entity->script.started || entity->script.RegistrationKey() != instance.assetPath ||
            scripts_.at(entity->script.RegistrationKey()).revision != instance.revision) retiring.push_back(id);
    }
    for (const auto id : retiring) StopInstance(world, id);

    // Callbacks may create or delete entities. Never iterate the world's vector
    // across a callback, and reacquire an entity after every user hook.
    std::vector<Scene::EntityId> ids;
    for (const auto& entity : world.GetEntities()) ids.push_back(entity.id);
    // Preserve native/legacy callback positions and their relative order.
    // Rotate only Lua slots, and only after a preceding budget deferral.
    std::vector<size_t> luaSlots;std::vector<Scene::EntityId> luaIds;
    for(size_t index=0;index<ids.size();++index){const auto* entity=world.Find(ids[index]);
        if(CanRun(entity) && scripts_.at(entity->script.RegistrationKey()).lua){luaSlots.push_back(index);luaIds.push_back(ids[index]);}}
    auto& nextStart=fixed?nextFixedLuaStart_:nextLuaStart_;
    const auto start=std::find(luaIds.begin(),luaIds.end(),nextStart);
    if(start!=luaIds.end())std::rotate(luaIds.begin(),start,luaIds.end());
    for(size_t index=0;index<luaSlots.size();++index)ids[luaSlots[index]]=luaIds[index];
    nextStart=0;
    const auto defer=[&](Scene::EntityId id){luaBudget_->DeferEntity();if(!nextStart)nextStart=id;};
    for (const auto id : ids) {
        auto* entity = world.Find(id);
        if (!CanRun(entity)) continue;
        const bool lua=scripts_.at(entity->script.RegistrationKey()).lua;
        // Never mark an unattempted VM as started/faulted merely because an
        // earlier Entity spent this frame's allowance.
        const auto existing=instances_.find(id);
        if(existing!=instances_.end() && !existing->second.faulted && !(fixed?existing->second.callbacks.onFixedUpdate:existing->second.callbacks.onUpdate)) {
            existing->second.lastRecord=*entity;continue;
        }
        if(lua && !luaBudget_->HasFrameAllowance() && (existing==instances_.end() || !existing->second.faulted)){defer(id);continue;}
        if (instances_.find(id) == instances_.end()) {
            const auto registration = scripts_.at(entity->script.RegistrationKey());
            entity->script.started = true;
            errors_.erase(id);
            auto [created,inserted]=instances_.emplace(id, Instance{entity->script.RegistrationKey(), registration.revision, {}, *entity});
            (void)inserted;
            try {
                created->second.callbacks=registration.factory();
                const auto onCreate=created->second.callbacks.onCreate;
                if(lua){
                    // Lua can atomically replace World storage. A detached
                    // hook record also retains final self state on deletion.
                    auto hookRecord=*entity;
                    if(onCreate)onCreate(hookRecord);
                    CommitLuaSelf(world,hookRecord);
                    created->second.lastRecord=hookRecord;
                } else if(onCreate)onCreate(*entity);
                // Create may replace the entire World storage. Do not use the
                // callback's borrowed record after it returns. Retain a live
                // successful Create even when Update is absent/deferred.
                entity=world.Find(id);
                if(entity && entity->hasScript && entity->script.RegistrationKey()==created->second.assetPath)
                    created->second.lastRecord=*entity;
            } catch (const LuaScriptError& exception) { created->second.faulted=true;RecordFault(id,exception.what()); }
        }
        entity = world.Find(id);
        const auto instance = instances_.find(id);
        if (instance == instances_.end()) continue;
        if (instance->second.faulted) continue;
        if (!CanRun(entity) || !entity->script.started || entity->script.RegistrationKey() != instance->second.assetPath ||
            scripts_.at(entity->script.RegistrationKey()).revision != instance->second.revision) {
            StopInstance(world, id);
            continue;
        }
        const auto update = fixed?instance->second.callbacks.onFixedUpdate:instance->second.callbacks.onUpdate;
        if(!update){instance->second.lastRecord=*entity;continue;}
        if(lua && !luaBudget_->HasFrameAllowance()){defer(id);continue;}
        try {
            if(lua){auto hookRecord=*entity;update(hookRecord,deltaSeconds);CommitLuaSelf(world,hookRecord);instances_.at(id).lastRecord=hookRecord;}
            else update(*entity,deltaSeconds);
        }
        catch (const LuaScriptError& exception) {
            instances_.at(id).faulted=true;RecordFault(id,exception.what());continue;
        }
        entity = world.Find(id);
        const auto running = instances_.find(id);
        if (running == instances_.end()) continue;
        if (!CanRun(entity) || !entity->script.started || entity->script.RegistrationKey() != running->second.assetPath ||
            scripts_.at(entity->script.RegistrationKey()).revision != running->second.revision) StopInstance(world, id);
        else running->second.lastRecord = *entity;
    }
}

void ScriptRuntime::DispatchTriggers(Scene::EntityWorld& world,std::span<const Physics::TriggerEvent> events)
{
    if (shuttingDown_ || events.empty()) return;
    ScriptSceneView::WriteScope sceneScope(*sceneView_,world,assetOwner_.get(),animatorReader_);
    LuaResourceBudget::FrameScope frame(*luaBudget_);
    const auto dispatch = [&](Scene::EntityId id,Scene::EntityId other,bool entered) {
        auto* entity = world.Find(id);
        const auto instance = instances_.find(id);
        if (instance==instances_.end() || instance->second.faulted || !CanRun(entity) ||
            !entity->script.started || entity->script.RegistrationKey()!=instance->second.assetPath) return;
        const auto& registration = scripts_.at(instance->second.assetPath);
        if (registration.revision!=instance->second.revision) return;
        const auto& hook = entered ? instance->second.callbacks.onTriggerEnter : instance->second.callbacks.onTriggerExit;
        if (!hook) return;
        if (registration.lua && !luaBudget_->HasFrameAllowance()) {luaBudget_->DeferEntity();return;}
        try {
            if (registration.lua) {
                auto record = *entity;
                hook(record,other);
                CommitLuaSelf(world,record);
                instance->second.lastRecord = record;
            } else {
                hook(*entity,other);
                if (const auto* updated = world.Find(id)) instance->second.lastRecord = *updated;
            }
        } catch (const LuaScriptError& exception) {
            instance->second.faulted = true;
            RecordFault(id,exception.what());
        }
    };
    for (const auto& event : events) {
        dispatch(event.first,event.second,event.entered);
        dispatch(event.second,event.first,event.entered);
    }
}

void ScriptRuntime::Shutdown(Scene::EntityWorld& world)
{
    if(shuttingDown_)return;
    // An executing Dispatch holds references into instances_. Never erase the
    // current callback/factory underneath it, even when Shutdown is reentrant.
    if(sceneView_->World())throw std::logic_error("Cannot shut down scripts during dispatch");
    shuttingDown_=true;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{shuttingDown_};
    ScriptSceneView::WriteScope sceneScope(*sceneView_,world,assetOwner_.get(),animatorReader_);
    LuaResourceBudget::FrameScope frame(*luaBudget_);nextLuaStart_=nextFixedLuaStart_=0;
    ResetInput();
    std::exception_ptr firstFailure;
    // StopInstance removes ownership BEFORE invoking user code. Reacquire begin
    // after each callback; shutdown dispatch admission prevents new instances.
    while(!instances_.empty()) {
        const auto id=instances_.begin()->first;
        try {StopInstance(world,id);}
        catch(...) {if(!firstFailure)firstFailure=std::current_exception();}
    }
    ResetInput();
    audioRequests_->Clear();
    animatorRequests_->Clear();
    sceneTransitions_->Clear();
    if(firstFailure)std::rethrow_exception(firstFailure);
}

bool ScriptRuntime::CanRun(const Scene::EntityRecord* entity) const
{
    return entity && entity->active && entity->hasScript && entity->script.enabled &&
        !entity->script.RegistrationKey().empty() && Contains(entity->script.RegistrationKey());
}

void ScriptRuntime::StopInstance(Scene::EntityWorld& world, Scene::EntityId id)
{
    const auto found = instances_.find(id);
    if (found == instances_.end()) return;
    // MSVC's map-containing EntityRecord move can allocate. Extract the owned
    // node instead: unregister first, keep callbacks/lastRecord alive in place.
    auto retired=instances_.extract(found);
    auto& instance=retired.mapped();
    auto* entity = world.Find(id);
    if (entity && entity->hasScript) {
        const auto& script=entity->script;
        const std::string_view key=instance.assetPath;
        const bool matches=script.assetGuid.empty() ? key==script.assetPath :
            key.starts_with("guid:") && key.substr(5)==script.assetGuid;
        if(matches)entity->script.started=false;
    }
    // A removed entity no longer has a writable world record. Its final retained
    // value lets destruction hooks release resources, without resurrecting it.
    if (!instance.faulted && instance.callbacks.onDestroy) {
        try { instance.callbacks.onDestroy(entity ? *entity : instance.lastRecord); }
        catch (const LuaScriptError& exception) { RecordFault(id,exception.what()); }
    }
}

void ScriptRuntime::RecordFault(Scene::EntityId id,const std::string& error)
{
    if(errors_.size()<256 || errors_.contains(id)) errors_[id]=error.substr(0,2048);
}
size_t ScriptRuntime::GetRunningCount() const
{
    size_t count=0;for(const auto& [id,instance]:instances_) { (void)id;if(!instance.faulted)++count; } return count;
}
std::string ScriptRuntime::GetLastError(Scene::EntityId id) const
{
    const auto found=errors_.find(id);return found==errors_.end() ? std::string{} : found->second;
}

bool ScriptRuntime::Contains(const std::string& assetPath) const
{
    return scripts_.find(assetPath) != scripts_.end();
}

ScriptProperties ScriptRuntime::GetPropertyDefaults(const std::string& key) const
{
    const auto found=scripts_.find(key);return found==scripts_.end() ? ScriptProperties{} : found->second.defaults;
}

} // namespace Engine::Scripting
