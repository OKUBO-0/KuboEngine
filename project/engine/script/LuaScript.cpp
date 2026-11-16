#include "LuaScript.h"
#include "ScriptSceneCommands.h"
extern "C" {
#include "externals/lua-5.4.9/src/lua.h"
#include "externals/lua-5.4.9/src/lauxlib.h"
#include "externals/lua-5.4.9/src/lualib.h"
}
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include <memory>
#include <utility>
#include <string_view>

namespace Engine::Scripting {
namespace {
constexpr size_t memoryLimit=8*1024*1024;
constexpr int instructionLimit=100000, instructionInterval=500;
constexpr const char* entityType="Kubo.Entity";
// Factory copies share one retained source reservation. The bounded staging
// read, string capacity/metadata and other native allocations are not VM heap.
class LuaSourceLease final {
public:
    LuaSourceLease(const std::string& source,std::shared_ptr<LuaResourceBudget> budget):budget_(std::move(budget)) {
        if(source.size()>1024*1024)throw LuaScriptError("Lua source exceeds 1 MiB");
        if(!budget_->Resize(0,source.size()))throw LuaScriptError("Lua retained source memory budget exceeded");
        try {bytes_=source;}catch(...) {budget_->Resize(source.size(),0);throw;}
    }
    ~LuaSourceLease(){budget_->Resize(bytes_.size(),0);}
    LuaSourceLease(const LuaSourceLease&)=delete;
    const std::string& Bytes() const noexcept {return bytes_;}
private:
    std::shared_ptr<LuaResourceBudget> budget_;
    std::string bytes_;
};
class LuaInstance final {
    struct StateLease {
        std::shared_ptr<LuaResourceBudget> budget;
        explicit StateLease(std::shared_ptr<LuaResourceBudget> value) : budget(std::move(value)) {
            if(!budget->AcquireState())throw LuaScriptError("Lua runtime VM budget exceeded");
        }
        ~StateLease(){budget->ReleaseState();}
        StateLease(const StateLease&)=delete;
    };
public:
    LuaInstance(const std::string& source,const std::string& name,std::shared_ptr<const InputSystem::InputFrame> input,std::shared_ptr<ScriptAudioRequests> audio,std::shared_ptr<LuaResourceBudget> budget,std::shared_ptr<const ScriptSceneView> scene,std::shared_ptr<ScriptAnimatorRequests> animator,std::shared_ptr<ScriptSceneTransitions> transitions) : lease_(std::move(budget)),input_(std::move(input)),audio_(std::move(audio)),scene_(std::move(scene)),animator_(std::move(animator)),transitions_(std::move(transitions)) {
        state_=lua_newstate(Allocate,this);
        if(!state_) throw LuaScriptError("Cannot allocate Lua state");
        *static_cast<LuaInstance**>(lua_getextraspace(state_))=this;
        try {
            lua_pushcfunction(state_,Initialize);
            ProtectedCall(0,0);
            if(luaL_loadbufferx(state_,source.data(),source.size(),name.c_str(),"t")!=LUA_OK) Fail();
            ProtectedCall(0,0);
            lua_pushcfunction(state_,CaptureHooks);ProtectedCall(0,0);
            lua_pushcfunction(state_,CaptureProperties);ProtectedCall(0,0);
        } catch(...) { lua_close(state_); state_=nullptr; throw; }
    }
    ~LuaInstance() { audioOwner_->alive=false;if(state_) lua_close(state_); }
    const ScriptProperties& Defaults() const { return defaults_; }
    bool HasHook(size_t index) const noexcept {return hooks_[index]!=LUA_NOREF;}
    void SetDefaults(const ScriptProperties& defaults) { defaults_=defaults; }
    void Call(size_t hook,Scene::EntityRecord& entity,float dt=0,Scene::EntityId other=0) {
        if(hooks_[hook]==LUA_NOREF) return;
        // All bound mutations are isolated until this hook succeeds.
        auto candidate=entity;
        const bool borrowedLive=scene_ && scene_->World() && scene_->World()->Find(entity.id)==&entity;
        audioOwner_->entity=entity.id;
        audioBatch_.clear();
        animatorBatch_.Clear();
        transition_.reset();
        invokingDestroy_ = hook==2;
        current_=&candidate;
        sceneCommands_.Begin(scene_.get(),candidate);
        try {
            lua_rawgeti(state_,LUA_REGISTRYINDEX,hooks_[hook]);
            lua_rawgeti(state_,LUA_REGISTRYINDEX,entityReference_);
            if(hook==1 || hook==3) lua_pushnumber(state_,dt);
            if(hook==4 || hook==5) lua_pushinteger(state_,static_cast<lua_Integer>(other));
            ProtectedCall(hook==1 || hook>=3?2:1,0);
            if(!sceneCommands_.Prepare())throw LuaScriptError("Scene command targets or write scope expired before publication");
            try {
                if(animator_) {
                    if(!animator_->PublishTogether(animatorBatch_,audio_.get(),audioBatch_))throw LuaScriptError("Animator/Audio request queue budget exceeded or host unavailable");
                } else if(!audioBatch_.empty() && (!audio_ || !audio_->Publish(audioBatch_))) throw LuaScriptError("Audio request queue budget exceeded or host unavailable");
            } catch(const std::exception& exception) {throw LuaScriptError(exception.what());}
            // Host contract forbids reentry/World mutation between prepare,
            // Audio admission and allocation-free scalar publication.
            if(sceneCommands_.OperationCount() && !sceneCommands_.PublishPrepared())throw LuaScriptError("Scene command publication refused");
            const bool replacedWorld=sceneCommands_.ReplacesWorld();
            sceneCommands_.Clear();
            audioBatch_.clear();
            animatorBatch_.Clear();
            current_=nullptr;
            // Commit only fields which Lua can mutate. Moving the full record
            // can allocate MSVC map sentinels in properties/Prefab JSON after
            // peer and Audio publication, violating the hook transaction.
            // Runtime passes Lua a detached hook record. Direct factory hosts
            // may still pass a live record, now invalid after World replacement.
            if(!replacedWorld || !borrowedLive)CommitSelf(entity,candidate);
            if (transition_) transitions_->Publish(std::move(*transition_));
            transition_.reset();
        } catch(...) { transition_.reset();audioOwner_->alive=false;audioBatch_.clear();animatorBatch_.Clear();sceneCommands_.Clear();current_=nullptr; lua_settop(state_,0); throw; }
    }
private:
    static void CommitSelf(Scene::EntityRecord& entity,const Scene::EntityRecord& candidate) noexcept {
        static_assert(std::is_nothrow_copy_assignable_v<Scene::TransformComponent> && std::is_nothrow_copy_assignable_v<Vector3>);
        entity.transform=candidate.transform;
        entity.active=candidate.active;
        // Physics component edits also update the detached hook record: the
        // dispatcher must not reapply a pre-addition velocity after World swap.
        entity.hasCollider=candidate.hasCollider;entity.collider=candidate.collider;
        entity.hasRigidBody=candidate.hasRigidBody;entity.rigidBody=candidate.rigidBody;
    }
    static LuaInstance& Self(lua_State* state) { return **static_cast<LuaInstance**>(lua_getextraspace(state)); }
    static void* Allocate(void* user,void* pointer,size_t oldSize,size_t newSize) {
        auto& self=*static_cast<LuaInstance*>(user);
        if(!pointer) oldSize=0; // Lua passes a type tag for new allocations.
        if(!newSize) { std::free(pointer); self.lease_.budget->Resize(oldSize,0);self.used_-=oldSize; return nullptr; }
        if(newSize>memoryLimit || self.used_-oldSize>memoryLimit-newSize) return nullptr;
        if(!self.lease_.budget->Resize(oldSize,newSize))return nullptr;
        void* result=std::realloc(pointer,newSize);
        if(result) self.used_=self.used_-oldSize+newSize;
        else self.lease_.budget->Resize(newSize,oldSize);
        return result;
    }
    static void InstructionHook(lua_State* state,lua_Debug*) {
        auto& self=Self(state);
        if(self.lease_.budget->FrameActive()){
            if(self.remaining_<=0)luaL_error(state,"Lua instruction budget exceeded");
            if(!self.lease_.budget->ChargeInstruction())luaL_error(state,"Lua frame instruction budget exceeded");
            --self.remaining_;return;
        }
        self.remaining_-=instructionInterval;
        if(self.remaining_<=0) luaL_error(state,"Lua instruction budget exceeded");
    }
    [[noreturn]] void Fail() {
        const char* message=lua_tostring(state_,-1);
        std::string error=message ? message : "Lua execution/memory failure";
        lua_settop(state_,0); throw LuaScriptError(error);
    }
    void ProtectedCall(int arguments,int results) {
        // Count hooks run before each Lua opcode. Per-opcode accounting while
        // dispatching prevents many short hooks from escaping a frame budget.
        remaining_=instructionLimit; lua_sethook(state_,InstructionHook,LUA_MASKCOUNT,lease_.budget->FrameActive()?1:instructionInterval);
        const int status=lua_pcall(state_,arguments,results,0);
        lua_sethook(state_,nullptr,0,0);
        if(status!=LUA_OK) Fail();
    }
    static Scene::EntityRecord& Entity(lua_State* state) {
        auto* owner=*static_cast<LuaInstance**>(luaL_checkudata(state,1,entityType));
        if(owner!=&Self(state) || !owner->current_) luaL_error(state,"Entity is available only during its script hook");
        return *owner->current_;
    }
    static float Number(lua_State* state,int index) {
        const auto value=luaL_checknumber(state,index);
        if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) luaL_error(state,"Entity value must be a finite float");
        return static_cast<float>(value);
    }
    static Vector3 Vector(lua_State* state) { return {Number(state,2),Number(state,3),Number(state,4)}; }
    static int PushVector(lua_State* state,const Vector3& value) { lua_pushnumber(state,value.x);lua_pushnumber(state,value.y);lua_pushnumber(state,value.z);return 3; }
    static int Id(lua_State* state) { lua_pushinteger(state,static_cast<lua_Integer>(Entity(state).id)); return 1; }
    static int Position(lua_State* state) { return PushVector(state,Entity(state).transform.position); }
    static int Rotation(lua_State* state) { return PushVector(state,Entity(state).transform.rotation); }
    static int Scale(lua_State* state) { return PushVector(state,Entity(state).transform.scale); }
    static int SetPosition(lua_State* state) { Entity(state).transform.position=Vector(state);return 0; }
    static int SetRotation(lua_State* state) { Entity(state).transform.rotation=Vector(state);return 0; }
    static int SetScale(lua_State* state) { Entity(state).transform.scale=Vector(state);return 0; }
    static int Translate(lua_State* state) {
        auto& entity=Entity(state);const auto value=entity.transform.position+Vector(state);
        if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z)) return luaL_error(state,"Translation overflow");
        entity.transform.position=value;return 0;
    }
    static int Velocity(lua_State* state) {
        auto& entity=Entity(state);if(!entity.hasRigidBody) return luaL_error(state,"Entity has no RigidBody");
        return PushVector(state,entity.rigidBody.velocity);
    }
    static int SetVelocity(lua_State* state) {
        auto& entity=Entity(state);if(!entity.hasRigidBody) return luaL_error(state,"Entity has no RigidBody");
        entity.rigidBody.velocity=Vector(state);return 0;
    }
    static int Active(lua_State* state) { lua_pushboolean(state,Entity(state).active);return 1; }
    static int SetActive(lua_State* state) {
        luaL_checktype(state,2,LUA_TBOOLEAN);Entity(state).active=lua_toboolean(state,2)!=0;return 0;
    }
    static int Property(lua_State* state) {
        const auto& entity=Entity(state);luaL_checktype(state,2,LUA_TSTRING);
        size_t length=0;const char* name=lua_tolstring(state,2,&length);
        if(!ValidScriptText(std::string_view(name,length),128,true)) return luaL_error(state,"Invalid script property name");
        const auto found=Self(state).defaults_.find(std::string_view(name,length));
        if(found==Self(state).defaults_.end()) {lua_pushnil(state);return 1;}
        const auto overrideValue=entity.script.properties.find(std::string_view(name,length));
        const auto& value=overrideValue==entity.script.properties.end() ? found->second : overrideValue->second;
        if(value.index()!=found->second.index() || !ValidScriptProperty(value)) return luaL_error(state,"Script property override has invalid type/value");
        if(const auto* number=std::get_if<double>(&value)) lua_pushnumber(state,*number);
        else if(const auto* boolean=std::get_if<bool>(&value)) lua_pushboolean(state,*boolean);
        else {const auto& text=std::get<std::string>(value);lua_pushlstring(state,text.data(),text.size());}
        return 1;
    }
    static int CaptureProperties(lua_State* state) {
        auto& defaults=Self(state).defaults_;
        lua_pushglobaltable(state);lua_pushliteral(state,"properties");lua_rawget(state,-2);
        if(lua_isnil(state,-1)) {lua_pop(state,2);return 0;}
        if(!lua_istable(state,-1)) return luaL_error(state,"properties must be a table");
        lua_pushnil(state);
        while(lua_next(state,-2)!=0) {
            if(lua_type(state,-2)!=LUA_TSTRING) return luaL_error(state,"Property names must be strings");
            size_t length=0;const char* key=lua_tolstring(state,-2,&length);
            if(!ValidScriptText(std::string_view(key,length),128,true) || defaults.size()>=64) return luaL_error(state,"Invalid/too many script properties");
            const auto type=lua_type(state,-1);
            if(type==LUA_TNUMBER) {
                const auto value=lua_tonumber(state,-1);
                if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)()) return luaL_error(state,"Property number must be a finite float");
            } else if(type==LUA_TSTRING) {
                size_t size=0;const char* text=lua_tolstring(state,-1,&size);
                if(!ValidScriptText(std::string_view(text,size),1024)) return luaL_error(state,"Invalid property string");
            } else if(type!=LUA_TBOOLEAN) return luaL_error(state,"Property defaults require numbers, booleans or strings");
            // Owning C++ temporaries are destroyed before any Lua API can longjmp.
            bool allocationFailed=false;
            try {
                if(type==LUA_TNUMBER) defaults.emplace(std::string(key,length),static_cast<double>(lua_tonumber(state,-1)));
                else if(type==LUA_TBOOLEAN) defaults.emplace(std::string(key,length),lua_toboolean(state,-1)!=0);
                else {size_t size=0;const char* text=lua_tolstring(state,-1,&size);defaults.emplace(std::string(key,length),std::string(text,size));}
            } catch(...) {allocationFailed=true;}
            if(allocationFailed) return luaL_error(state,"Cannot allocate script property defaults");
            lua_pop(state,1);
        }
        lua_pop(state,2);return 0;
    }
    static const InputSystem::InputFrame& Input(lua_State* state) {
        auto& self=Self(state);
        if(!self.current_ || !self.input_) luaL_error(state,"Input is available only during a script hook");
        return *self.input_;
    }
    static const InputSystem::ActionFrame* Action(lua_State* state,const InputSystem::InputFrame& frame) {
        luaL_checktype(state,1,LUA_TSTRING);
        size_t length=0;const char* name=lua_tolstring(state,1,&length);
        if(length==0 || length>256) luaL_error(state,"Action name must contain 1 to 256 bytes");
        // No owning C++ temporary across a Lua longjmp/allocating API.
        for(const auto& [key,value]:frame.actions) if(std::string_view(key)==std::string_view(name,length)) return &value;
        return nullptr;
    }
    static int InputButton(lua_State* state) {
        const auto& frame=Input(state);
        const int domain=static_cast<int>(lua_tointeger(state,lua_upvalueindex(1)));
        const int field=static_cast<int>(lua_tointeger(state,lua_upvalueindex(2)));
        InputSystem::ButtonFrame button;
        if(domain==3) {
            const auto code=luaL_checkinteger(state,1);
            if(code<0 || code>=static_cast<lua_Integer>(InputSystem::GamepadButton::Count))return luaL_error(state,"Pad button out of range");
            const auto& pad=frame.pad.buttons[static_cast<size_t>(code)];
            button={pad.down,pad.pressed,pad.released};
        }
        else if(domain==2) { if(const auto* action=Action(state,frame)) button=action->button; }
        else {
            const auto code=luaL_checkinteger(state,1);
            if(code<0 || code>=(domain==0 ? 256 : 8)) return luaL_error(state,"Input code out of range");
            button=domain==0 ? frame.keys[static_cast<size_t>(code)] : frame.mouse[static_cast<size_t>(code)];
        }
        lua_pushboolean(state,frame.available && (field==0 ? button.down : field==1 ? button.pressed : button.released));return 1;
    }
    static int InputAxis(lua_State* state) {
        const auto& frame=Input(state);const auto* action=Action(state,frame);
        return PushVector(state,frame.available && action ? action->axis : Vector3{});
    }
    static int InputAvailable(lua_State* state) { lua_pushboolean(state,Input(state).available);return 1; }
    static int InputMousePosition(lua_State* state) { const auto& frame=Input(state);lua_pushnumber(state,frame.available ? frame.mouseX : 0);lua_pushnumber(state,frame.available ? frame.mouseY : 0);return 2; }
    static int InputMouseDelta(lua_State* state) { const auto& frame=Input(state);lua_pushnumber(state,frame.available ? frame.deltaX : 0);lua_pushnumber(state,frame.available ? frame.deltaY : 0);return 2; }
    static int InputWheel(lua_State* state) { const auto& frame=Input(state);lua_pushnumber(state,frame.available ? frame.wheel : 0);return 1; }
    static const Scene::EntityWorld& SceneWorld(lua_State* state) {
        auto& self=Self(state);
        const auto* world=self.sceneCommands_.ReadWorld();
        if(!self.current_ || !world)luaL_error(state,"Scene is available only during a host-owned script hook");
        return *world;
    }
    static Scene::EntityId SceneId(lua_State* state,int index=1,bool allowRoot=false) {
        luaL_checktype(state,index,LUA_TNUMBER);
        const auto id=luaL_checkinteger(state,index);
        if(id<0 || (!id && !allowRoot) || static_cast<lua_Unsigned>(id)>Scene::EntityWorld::MaximumEntityId)luaL_error(state,"Scene Entity ID out of range");
        return static_cast<Scene::EntityId>(id);
    }
    static const Scene::EntityRecord* SceneEntity(lua_State* state) {
        const auto& world=SceneWorld(state);const auto id=SceneId(state);
        const auto* entity=world.Find(id);
        // Read-your-own-writes from the hook's transactional candidate, but a
        // retained on_destroy record must never appear to resurrect an Entity.
        if(entity && Self(state).current_->id==id)return Self(state).current_;
        return entity;
    }
    static int SceneFind(lua_State* state) {
        const auto& world=SceneWorld(state);luaL_checktype(state,1,LUA_TSTRING);
        size_t length=0;const char* name=lua_tolstring(state,1,&length);
        if(!length || length>1024) return luaL_error(state,"Scene name must contain 1 to 1024 bytes");
        Scene::EntityId match=0;
        for(const auto& entity:world.GetEntities())if(std::string_view(entity.name)==std::string_view(name,length) && (!match || entity.id<match))match=entity.id;
        if(match)lua_pushinteger(state,static_cast<lua_Integer>(match));else lua_pushnil(state);return 1;
    }
    static int SceneCount(lua_State* state){lua_pushinteger(state,static_cast<lua_Integer>(SceneWorld(state).GetEntities().size()));return 1;}
    static int SceneExists(lua_State* state){lua_pushboolean(state,SceneEntity(state)!=nullptr);return 1;}
    static int SceneName(lua_State* state){const auto* entity=SceneEntity(state);if(entity)lua_pushlstring(state,entity->name.data(),entity->name.size());else lua_pushnil(state);return 1;}
    static int SceneParent(lua_State* state){const auto& world=SceneWorld(state);const auto* entity=world.Find(SceneId(state));if(entity)lua_pushinteger(state,static_cast<lua_Integer>(entity->parent));else lua_pushnil(state);return 1;}
    static int SceneActive(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);bool value=false;
        if(Self(state).sceneCommands_.Active(id,value))lua_pushboolean(state,value);else lua_pushnil(state);return 1;
    }
    static int SceneVelocity(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);const auto* value=Self(state).sceneCommands_.Velocity(id);
        if(value)return PushVector(state,*value);lua_pushnil(state);return 1;
    }
    static int SceneCommand(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);
        const auto operation=static_cast<ScriptSceneCommands::Operation>(lua_tointeger(state,lua_upvalueindex(1)));
        Vector3 value{};bool active=false;
        if(operation==ScriptSceneCommands::Operation::Active){luaL_checktype(state,2,LUA_TBOOLEAN);active=lua_toboolean(state,2)!=0;}
        else{for(int index=2;index<=4;++index)luaL_checktype(state,index,LUA_TNUMBER);value=Vector(state);}
        const auto result=Self(state).sceneCommands_.Apply(id,operation,value,active);
        if(result==ScriptSceneCommands::Result::Invalid)return luaL_error(state,"Invalid Scene value or translation overflow");
        if(result==ScriptSceneCommands::Result::Unavailable)return luaL_error(state,"Scene mutation requires an active host-owned write scope");
        if(result==ScriptSceneCommands::Result::Limit)return luaL_error(state,"Scene command budget exceeded (64 per hook)");
        if(result==ScriptSceneCommands::Result::AllocationFailure)return luaL_error(state,"Cannot allocate Scene transaction");
        lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneStructuralError(lua_State* state,ScriptSceneCommands::Result result){
        if(result==ScriptSceneCommands::Result::Invalid)return luaL_error(state,"Invalid Scene UTF-8 name, ID, hierarchy, component or exhausted Entity IDs");
        if(result==ScriptSceneCommands::Result::Unavailable)return luaL_error(state,"Scene mutation requires an active host-owned write scope");
        if(result==ScriptSceneCommands::Result::Limit)return luaL_error(state,"Scene transaction budget exceeded (64 operations/created/deleted Entities, 10000 Entities; Prefab admission 8 MiB/512 references/4096 file checks per hook)");
        if(result==ScriptSceneCommands::Result::AllocationFailure)return luaL_error(state,"Cannot allocate Scene transaction");
        return 0;
    }
    static int SceneAnimator(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);const auto* settings=Self(state).sceneCommands_.Animator(id);
        if(!settings){lua_pushnil(state);return 1;}
        lua_pushlstring(state,settings->clip.data(),settings->clip.size());lua_pushnumber(state,settings->speed);
        lua_pushboolean(state,settings->loop);lua_pushboolean(state,settings->playOnAwake);return 4;
    }
    // Only trivial component values/views cross Lua calls: allocation errors
    // longjmp. Never build an owning JSON/string/vector in these C callbacks.
    static_assert(std::is_trivially_destructible_v<Scene::LightComponent> && std::is_trivially_destructible_v<Scene::MaterialComponent>);
    static void RenderKeys(lua_State* state,int table,std::initializer_list<std::string_view> fields){
        table=lua_absindex(state,table);luaL_checktype(state,table,LUA_TTABLE);lua_pushnil(state);
        while(lua_next(state,table)){
            if(lua_type(state,-2)!=LUA_TSTRING)luaL_error(state,"Render settings require named fields");
            size_t size=0;const auto* key=lua_tolstring(state,-2,&size);bool known=false;
            for(const auto field:fields)if(field==std::string_view(key,size)){known=true;break;}
            if(!known)luaL_error(state,"Unknown render settings field");lua_pop(state,1);
        }
    }
    static void RenderBoolean(lua_State* state,int table,const char* key,bool& output){
        lua_pushstring(state,key);lua_rawget(state,table);
        if(!lua_isnil(state,-1)){luaL_checktype(state,-1,LUA_TBOOLEAN);output=lua_toboolean(state,-1)!=0;}lua_pop(state,1);
    }
    static float RenderNumber(lua_State* state,int index,double lo,double hi){
        luaL_checktype(state,index,LUA_TNUMBER);const double value=lua_tonumber(state,index);
        if(!std::isfinite(value) || value<lo || value>hi)luaL_error(state,"Invalid finite render setting or range");return static_cast<float>(value);
    }
    static void RenderScalar(lua_State* state,int table,const char* key,float& output,double lo,double hi){
        lua_pushstring(state,key);lua_rawget(state,table);if(!lua_isnil(state,-1))output=RenderNumber(state,-1,lo,hi);lua_pop(state,1);
    }
    static void RenderColor(lua_State* state,int table,float* output,int count){
        lua_pushliteral(state,"color");lua_rawget(state,table);
        if(lua_isnil(state,-1)){lua_pop(state,1);return;}luaL_checktype(state,-1,LUA_TTABLE);
        const auto colors=lua_absindex(state,-1);int seen=0;lua_pushnil(state);
        while(lua_next(state,colors)){
            if(!lua_isinteger(state,-2))luaL_error(state,"Render color requires a dense numeric array");
            const auto index=lua_tointeger(state,-2);if(index<1 || index>count)luaL_error(state,"Wrong render color size");
            output[index-1]=RenderNumber(state,-1,0,1);++seen;lua_pop(state,1);
        }
        if(seen!=count)luaL_error(state,"Wrong render color size");lua_pop(state,1);
    }
    static void PushRenderColor(lua_State* state,std::initializer_list<float> values){
        lua_createtable(state,static_cast<int>(values.size()),0);lua_Integer i=0;
        for(const auto v:values){lua_pushnumber(state,v);lua_rawseti(state,-2,++i);}lua_setfield(state,-2,"color");
    }
    static void PushRenderScalar(lua_State* state,const char* name,float value){lua_pushnumber(state,value);lua_setfield(state,-2,name);}
    static int SceneLight(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"scene.light expects one Entity ID");
        const auto* settings=Self(state).sceneCommands_.Light(SceneId(state));if(!settings){lua_pushnil(state);return 1;}
        if(!settings->IsValid())return luaL_error(state,"Invalid authored Light settings");
        const auto value=*settings;lua_createtable(state,0,8);
        lua_pushstring(state,value.kind==Scene::LightKind::Directional?"directional":value.kind==Scene::LightKind::Point?"point":"spot");lua_setfield(state,-2,"type");
        lua_pushboolean(state,value.enabled);lua_setfield(state,-2,"enabled");PushRenderColor(state,{value.color.x,value.color.y,value.color.z});
        PushRenderScalar(state,"intensity",value.intensity);PushRenderScalar(state,"range",value.range);PushRenderScalar(state,"decay",value.decay);
        PushRenderScalar(state,"innerAngle",value.innerAngle);PushRenderScalar(state,"outerAngle",value.outerAngle);return 1;
    }
    static int SceneMaterial(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"scene.material expects one Entity ID");
        const auto* settings=Self(state).sceneCommands_.Material(SceneId(state));if(!settings){lua_pushnil(state);return 1;}
        if(!settings->IsValid())return luaL_error(state,"Invalid authored Material settings");
        const auto value=*settings;lua_createtable(state,0,6);lua_pushboolean(state,value.lighting);lua_setfield(state,-2,"lighting");
        PushRenderColor(state,{value.color.x,value.color.y,value.color.z,value.color.w});PushRenderScalar(state,"shininess",value.shininess);
        PushRenderScalar(state,"roughness",value.roughness);PushRenderScalar(state,"reflectionStrength",value.reflectionStrength);PushRenderScalar(state,"textureInfluence",value.textureInfluence);return 1;
    }
    static int SceneSetLight(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=2)return luaL_error(state,"scene.set_light expects an ID and settings table");
        const auto id=SceneId(state);RenderKeys(state,2,{"type","enabled","color","intensity","range","decay","innerAngle","outerAngle"});Scene::LightComponent value;
        lua_pushliteral(state,"type");lua_rawget(state,2);
        if(!lua_isnil(state,-1)){
            luaL_checktype(state,-1,LUA_TSTRING);size_t size=0;const auto* text=lua_tolstring(state,-1,&size);const std::string_view type(text,size);
            if(type=="directional")value.kind=Scene::LightKind::Directional;else if(type=="point")value.kind=Scene::LightKind::Point;else if(type=="spot")value.kind=Scene::LightKind::Spot;else return luaL_error(state,"Unknown Light type");
        }lua_pop(state,1);
        RenderBoolean(state,2,"enabled",value.enabled);float color[]{1,1,1};RenderColor(state,2,color,3);value.color={color[0],color[1],color[2]};
        RenderScalar(state,2,"intensity",value.intensity,0,1000000);RenderScalar(state,2,"range",value.range,double(0.0001f),1000000);RenderScalar(state,2,"decay",value.decay,0,16);
        RenderScalar(state,2,"innerAngle",value.innerAngle,0,Scene::MaximumLightAngle);RenderScalar(state,2,"outerAngle",value.outerAngle,double(0.0001f),Scene::MaximumLightAngle);
        const auto result=Self(state).sceneCommands_.SetLight(id,value);SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static void UIPair(lua_State* state,int table,const char* key,Vector2& output,double lo,double hi){
        lua_pushstring(state,key);lua_rawget(state,table);if(lua_isnil(state,-1)){lua_pop(state,1);return;}
        luaL_checktype(state,-1,LUA_TTABLE);const auto values=lua_absindex(state,-1);int seen=0;float result[2]{};lua_pushnil(state);
        while(lua_next(state,values)){
            if(!lua_isinteger(state,-2))luaL_error(state,"UI vector requires a dense two-number array");
            const auto index=lua_tointeger(state,-2);if(index<1 || index>2)luaL_error(state,"UI vector requires two values");
            result[index-1]=RenderNumber(state,-1,lo,hi);++seen;lua_pop(state,1);
        }
        if(seen!=2)luaL_error(state,"UI vector requires two values");lua_pop(state,1);output={result[0],result[1]};
    }
    static void PushUIPair(lua_State* state,const char* name,Vector2 value){
        lua_createtable(state,2,0);lua_pushnumber(state,value.x);lua_rawseti(state,-2,1);lua_pushnumber(state,value.y);lua_rawseti(state,-2,2);lua_setfield(state,-2,name);
    }
    static std::string_view UIString(lua_State* state,int table,const char* key){
        lua_pushstring(state,key);lua_rawget(state,table);std::string_view result;
        if(!lua_isnil(state,-1)){luaL_checktype(state,-1,LUA_TSTRING);size_t size=0;const auto* value=lua_tolstring(state,-1,&size);result={value,size};}
        // The caller's settings table pins this string until SetSprite copies it.
        lua_pop(state,1);return result;
    }
    static int UIOrder(lua_State* state,int table){
        lua_pushliteral(state,"sortOrder");lua_rawget(state,table);int result=0;
        if(!lua_isnil(state,-1)){
            luaL_checktype(state,-1,LUA_TNUMBER);const double n=lua_tonumber(state,-1);
            if(!std::isfinite(n) || n<-32768 || n>32767 || std::floor(n)!=n)luaL_error(state,"UI sortOrder requires an integer -32768..32767");
            result=static_cast<int>(n);
        }lua_pop(state,1);return result;
    }
    static int SceneCanvas(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"scene.canvas expects one Entity ID");
        const auto* settings=Self(state).sceneCommands_.Canvas(SceneId(state));if(!settings){lua_pushnil(state);return 1;}
        if(!settings->IsValid())return luaL_error(state,"Invalid authored Canvas");
        const auto value=*settings;lua_createtable(state,0,4);lua_pushboolean(state,value.enabled);lua_setfield(state,-2,"enabled");
        PushUIPair(state,"referenceSize",value.referenceSize);lua_pushstring(state,value.scaleMode==Scene::CanvasScaleMode::ConstantPixels?"constantPixels":"scaleWithOutput");lua_setfield(state,-2,"scaleMode");
        lua_pushinteger(state,value.sortOrder);lua_setfield(state,-2,"sortOrder");return 1;
    }
    static int SceneSprite(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"scene.sprite expects one Entity ID");
        const auto* value=Self(state).sceneCommands_.Sprite(SceneId(state));if(!value){lua_pushnil(state);return 1;}
        if(!value->IsValid())return luaL_error(state,"Invalid authored Sprite");
        // Borrow live immutable fields; no owning SpriteComponent crosses Lua allocation/longjmp.
        lua_createtable(state,0,9);lua_pushboolean(state,value->enabled);lua_setfield(state,-2,"enabled");
        lua_pushlstring(state,value->assetPath.data(),value->assetPath.size());lua_setfield(state,-2,"asset");
        if(!value->assetGuid.empty()){lua_pushlstring(state,value->assetGuid.data(),value->assetGuid.size());lua_setfield(state,-2,"guid");}
        PushUIPair(state,"size",value->size);PushUIPair(state,"pivot",value->pivot);PushRenderColor(state,{value->color.x,value->color.y,value->color.z,value->color.w});
        lua_pushboolean(state,value->flipX);lua_setfield(state,-2,"flipX");lua_pushboolean(state,value->flipY);lua_setfield(state,-2,"flipY");lua_pushinteger(state,value->sortOrder);lua_setfield(state,-2,"sortOrder");return 1;
    }
    static int SceneSetCanvas(lua_State* state){
        static_assert(std::is_trivially_destructible_v<Scene::CanvasComponent>);
        SceneWorld(state);if(lua_gettop(state)!=2)return luaL_error(state,"scene.set_canvas expects ID and settings table");
        const auto id=SceneId(state);RenderKeys(state,2,{"enabled","referenceSize","scaleMode","sortOrder"});Scene::CanvasComponent value;
        RenderBoolean(state,2,"enabled",value.enabled);UIPair(state,2,"referenceSize",value.referenceSize,1,16384);value.sortOrder=UIOrder(state,2);
        const auto mode=UIString(state,2,"scaleMode");
        // Empty string is invalid when explicitly supplied, unlike omitted nil.
        lua_pushliteral(state,"scaleMode");lua_rawget(state,2);const bool supplied=!lua_isnil(state,-1);lua_pop(state,1);
        if(supplied){if(mode=="constantPixels")value.scaleMode=Scene::CanvasScaleMode::ConstantPixels;else if(mode!="scaleWithOutput")return luaL_error(state,"Unknown Canvas scaling mode");}
        const auto result=Self(state).sceneCommands_.SetCanvas(id,value);SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneSetSprite(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=2)return luaL_error(state,"scene.set_sprite expects ID and settings table");
        const auto id=SceneId(state);RenderKeys(state,2,{"enabled","asset","guid","size","pivot","color","flipX","flipY","sortOrder"});ScriptSceneCommands::SpriteInput value;
        value.asset=UIString(state,2,"asset");value.guid=UIString(state,2,"guid");value.sortOrder=UIOrder(state,2);
        RenderBoolean(state,2,"enabled",value.enabled);RenderBoolean(state,2,"flipX",value.flipX);RenderBoolean(state,2,"flipY",value.flipY);
        UIPair(state,2,"size",value.size,0,16384);UIPair(state,2,"pivot",value.pivot,-16,16);float color[]{1,1,1,1};RenderColor(state,2,color,4);value.color={color[0],color[1],color[2],color[3]};
        const auto result=Self(state).sceneCommands_.SetSprite(id,value);SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneRemoveUI(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"UI removal expects one Entity ID");
        const auto id=SceneId(state);const auto result=lua_toboolean(state,lua_upvalueindex(1))?Self(state).sceneCommands_.RemoveSprite(id):Self(state).sceneCommands_.RemoveCanvas(id);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneSetMaterial(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=2)return luaL_error(state,"scene.set_material expects an ID and settings table");
        const auto id=SceneId(state);RenderKeys(state,2,{"lighting","color","shininess","roughness","reflectionStrength","textureInfluence"});Scene::MaterialComponent value;
        RenderBoolean(state,2,"lighting",value.lighting);float color[]{1,1,1,1};RenderColor(state,2,color,4);value.color={color[0],color[1],color[2],color[3]};
        RenderScalar(state,2,"shininess",value.shininess,1,1024);RenderScalar(state,2,"roughness",value.roughness,0,1);RenderScalar(state,2,"reflectionStrength",value.reflectionStrength,0,1);RenderScalar(state,2,"textureInfluence",value.textureInfluence,0,1);
        const auto result=Self(state).sceneCommands_.SetMaterial(id,value);SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneRemoveRender(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)!=1)return luaL_error(state,"Render component removal expects one Entity ID");
        const auto id=SceneId(state);const auto result=lua_toboolean(state,lua_upvalueindex(1))?Self(state).sceneCommands_.RemoveMaterial(id):Self(state).sceneCommands_.RemoveLight(id);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneCollider(lua_State* state){
        SceneWorld(state);const auto* value=Self(state).sceneCommands_.Collider(SceneId(state));
        if(!value){lua_pushnil(state);return 1;}
        PushVector(state,value->halfExtents);lua_pushboolean(state,value->isTrigger);
        lua_pushnumber(state,value->friction);lua_pushnumber(state,value->restitution);return 6;
    }
    static int SceneRigidBody(lua_State* state){
        SceneWorld(state);Physics::RigidBodyComponent value;
        if(!Self(state).sceneCommands_.RigidBody(SceneId(state),value)){lua_pushnil(state);return 1;}
        PushVector(state,value.velocity);lua_pushnumber(state,value.mass);
        lua_pushboolean(state,value.dynamic);lua_pushboolean(state,value.useGravity);lua_pushboolean(state,value.grounded);return 7;
    }
    static float PhysicsNumber(lua_State* state,int index,double minimum,double maximum){
        luaL_checktype(state,index,LUA_TNUMBER);const auto value=lua_tonumber(state,index);
        if(!std::isfinite(value) || value<minimum || value>maximum)luaL_error(state,"Invalid finite Physics value or range");
        return static_cast<float>(value);
    }
    static int SceneSetCollider(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);Physics::ColliderComponent value;
        constexpr auto maximum=(std::numeric_limits<float>::max)();
        value.halfExtents={PhysicsNumber(state,2,0,maximum),PhysicsNumber(state,3,0,maximum),PhysicsNumber(state,4,0,maximum)};
        luaL_checktype(state,5,LUA_TBOOLEAN);value.isTrigger=lua_toboolean(state,5)!=0;
        value.friction=PhysicsNumber(state,6,0,1);value.restitution=PhysicsNumber(state,7,0,1);
        const auto result=Self(state).sceneCommands_.SetCollider(id,value);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneColliderShape(lua_State* state) {
        SceneWorld(state);
        const auto* value=Self(state).sceneCommands_.Collider(SceneId(state));
        if (!value) {lua_pushnil(state);return 1;}
        lua_pushstring(state,value->shape==Physics::ColliderShape::Sphere ? "sphere" :
            value->shape==Physics::ColliderShape::Capsule ? "capsule" : "box");
        lua_pushnumber(state,value->radius);
        lua_pushnumber(state,value->halfSegment);
        return 3;
    }
    static int SceneSetRoundCollider(lua_State* state) {
        SceneWorld(state);
        const auto id=SceneId(state);
        const char* const shapes[]={"sphere","capsule",nullptr};
        Physics::ColliderComponent value;
        value.shape=luaL_checkoption(state,2,nullptr,shapes)==0 ?
            Physics::ColliderShape::Sphere : Physics::ColliderShape::Capsule;
        constexpr auto maximum=(std::numeric_limits<float>::max)();
        value.radius=PhysicsNumber(state,3,(std::numeric_limits<float>::denorm_min)(),maximum);
        value.halfSegment=PhysicsNumber(state,4,0,maximum);
        luaL_checktype(state,5,LUA_TBOOLEAN);
        value.isTrigger=lua_toboolean(state,5)!=0;
        value.friction=PhysicsNumber(state,6,0,1);
        value.restitution=PhysicsNumber(state,7,0,1);
        const auto result=Self(state).sceneCommands_.SetCollider(id,value);
        SceneStructuralError(state,result);
        lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);
        return 1;
    }
    static Physics::PhysicsQueryFilter QueryFilter(lua_State* state,int first){
        Physics::PhysicsQueryFilter filter;
        if(lua_gettop(state)>=first)filter.ignore=SceneId(state,first,true);
        if(lua_gettop(state)>=first+1){luaL_checktype(state,first+1,LUA_TBOOLEAN);filter.includeTriggers=lua_toboolean(state,first+1)!=0;}
        return filter;
    }
    static int PhysicsQueryError(lua_State* state,ScriptSceneCommands::Result result){
        if(result==ScriptSceneCommands::Result::Success)return 0;
        if(result==ScriptSceneCommands::Result::Limit)return luaL_error(state,"Physics query budget exceeded (64 queries, 262144 logical work per hook; 10000 Entities, 1024 overlap hits)");
        if(result==ScriptSceneCommands::Result::AllocationFailure)return luaL_error(state,"Cannot allocate Physics query");
        if(result==ScriptSceneCommands::Result::Unavailable)return luaL_error(state,"Physics query scope expired or unavailable");
        return luaL_error(state,"Invalid Physics query, ray direction, bounds or World identity");
    }
    static int SceneRaycast(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)<7 || lua_gettop(state)>9)return luaL_error(state,"scene.raycast expects 7 to 9 arguments");
        constexpr auto maximum=(std::numeric_limits<float>::max)();
        const Vector3 origin{PhysicsNumber(state,1,-maximum,maximum),PhysicsNumber(state,2,-maximum,maximum),PhysicsNumber(state,3,-maximum,maximum)};
        const Vector3 direction{PhysicsNumber(state,4,-maximum,maximum),PhysicsNumber(state,5,-maximum,maximum),PhysicsNumber(state,6,-maximum,maximum)};
        const float distance=PhysicsNumber(state,7,0,maximum);const auto filter=QueryFilter(state,8);Physics::PhysicsRayHit hit;
        PhysicsQueryError(state,Self(state).sceneCommands_.Raycast(origin,direction,distance,hit,filter));
        if(!hit.entity){lua_pushnil(state);return 1;}
        lua_pushinteger(state,static_cast<lua_Integer>(hit.entity));lua_pushnumber(state,hit.distance);PushVector(state,hit.position);PushVector(state,hit.normal);lua_pushboolean(state,hit.trigger);return 9;
    }
    static int SceneOverlapBox(lua_State* state){
        SceneWorld(state);if(lua_gettop(state)<6 || lua_gettop(state)>8)return luaL_error(state,"scene.overlap_box expects 6 to 8 arguments");
        constexpr auto maximum=(std::numeric_limits<float>::max)();
        const Vector3 center{PhysicsNumber(state,1,-maximum,maximum),PhysicsNumber(state,2,-maximum,maximum),PhysicsNumber(state,3,-maximum,maximum)};
        const Vector3 half{PhysicsNumber(state,4,0,maximum),PhysicsNumber(state,5,0,maximum),PhysicsNumber(state,6,0,maximum)};
        const auto filter=QueryFilter(state,7);auto& self=Self(state);
        PhysicsQueryError(state,self.sceneCommands_.OverlapBox(center,half,self.queryHits_,filter));
        // Retained outside this C frame: Lua allocation errors longjmp. No
        // automatic vector/string may own native storage across lua_* pushes.
        lua_createtable(state,static_cast<int>(self.queryHits_.size()),0);
        for(size_t i=0;i<self.queryHits_.size();++i){lua_pushinteger(state,static_cast<lua_Integer>(self.queryHits_[i]));lua_rawseti(state,-2,static_cast<lua_Integer>(i+1));}return 1;
    }
    static int SceneSetRigidBody(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);Physics::RigidBodyComponent value;
        constexpr auto maximum=(std::numeric_limits<float>::max)();
        value.velocity={PhysicsNumber(state,2,-maximum,maximum),PhysicsNumber(state,3,-maximum,maximum),PhysicsNumber(state,4,-maximum,maximum)};
        value.mass=PhysicsNumber(state,5,0,maximum);
        luaL_checktype(state,6,LUA_TBOOLEAN);luaL_checktype(state,7,LUA_TBOOLEAN);
        value.dynamic=lua_toboolean(state,6)!=0;value.useGravity=lua_toboolean(state,7)!=0;
        const auto result=Self(state).sceneCommands_.SetRigidBody(id,value);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneRemovePhysics(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);
        const auto result=lua_toboolean(state,lua_upvalueindex(1)) ? Self(state).sceneCommands_.RemoveRigidBody(id) : Self(state).sceneCommands_.RemoveCollider(id);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneAnimatorCommand(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);auto& self=Self(state);
        if(!self.animator_ || !self.animator_->Enabled())return luaL_error(state,"Animator command host unavailable");
        const auto command=static_cast<AnimationSystem::AnimatorCommand>(lua_tointeger(state,lua_upvalueindex(1)));
        const auto result=self.sceneCommands_.QueueAnimator(id,command,self.animatorBatch_);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneAnimatorState(lua_State* state){
        const auto& world=SceneWorld(state);const auto id=SceneId(state);auto& self=Self(state);
        const auto* reader=self.scene_->AnimatorReader();
        if(!reader || reader->OwnerWorld()!=self.scene_->World())return luaL_error(state,"Animator state host unavailable or wrong World");
        const auto* settings=self.sceneCommands_.Animator(id);const auto* entity=world.Find(id);
        if(!entity || !settings){lua_pushnil(state);return 1;}
        bool active=false;AnimationSystem::AnimatorPlaybackState value;
        if(self.sceneCommands_.Active(id,active) && active && entity->hasModel)value=reader->ReadAnimatorState(*entity,*settings);
        // A trusted reader's nonfinite/negative sample is unavailable, not a Lua
        // number. Pending commands do not synthesize future playback state.
        if(!value.ready || !std::isfinite(value.time) || value.time<0)value={};
        lua_pushboolean(state,value.ready);lua_pushboolean(state,value.ready && value.playing);
        if(value.ready)lua_pushnumber(state,value.time);else lua_pushnil(state);return 3;
    }
    static size_t ControllerLayerIndex(lua_State* state,int argument,bool optional) {
        if(optional && lua_isnoneornil(state,argument))return 0;
        luaL_checktype(state,argument,LUA_TNUMBER);const double value=lua_tonumber(state,argument);
        if(!std::isfinite(value) || value<0 || value>=4 || std::floor(value)!=value)luaL_error(state,"Controller layer must be an integer in 0..3");
        return static_cast<size_t>(value);
    }
    static int SceneControllerState(lua_State* state){
        const auto& world=SceneWorld(state);const auto id=SceneId(state);auto& self=Self(state);
        const auto layer=ControllerLayerIndex(state,2,true);
        const auto* reader=self.scene_->AnimatorReader();
        if(!reader || reader->OwnerWorld()!=self.scene_->World())return luaL_error(state,"Controller state host unavailable or wrong World");
        const auto* settings=self.sceneCommands_.Animator(id);const auto* entity=world.Find(id);
        std::string_view value;bool active=false;
        if(entity && settings && !settings->controllerGuid.empty() && self.sceneCommands_.Active(id,active) && active && entity->hasModel) {
            if(layer==0)value=reader->ReadControllerState(*entity,*settings);
            else{const auto sample=reader->ReadControllerLayer(*entity,*settings,layer);if(sample.ready)value=sample.state;}
        }
        if(!ValidScriptText(value,128,true)){lua_pushnil(state);return 1;}
        lua_pushlstring(state,value.data(),value.size());return 1;
    }
    static int SceneControllerLayerValue(lua_State* state) {
        const auto& world=SceneWorld(state);const auto id=SceneId(state);const auto layer=ControllerLayerIndex(state,2,true);auto& self=Self(state);
        const auto* reader=self.scene_->AnimatorReader();
        if(!reader || reader->OwnerWorld()!=self.scene_->World())return luaL_error(state,"Controller layer host unavailable or wrong World");
        const auto* settings=self.sceneCommands_.Animator(id);const auto* entity=world.Find(id);
        AnimationSystem::ControllerLayerState value;bool active=false;
        if(entity && settings && !settings->controllerGuid.empty() && self.sceneCommands_.Active(id,active) && active && entity->hasModel)
            value=reader->ReadControllerLayer(*entity,*settings,layer);
        if(!value.ready || !ValidScriptText(value.state,128,true) || !std::isfinite(value.time) || value.time<0 ||
            !std::isfinite(value.weight) || value.weight<0 || value.weight>1 || !std::isfinite(value.progress) || value.progress<0 || value.progress>1e9 ||
            !std::isfinite(value.normalized) || value.normalized<0 || value.normalized>1){lua_pushnil(state);return 1;}
        const auto field=lua_tointeger(state,lua_upvalueindex(1));
        lua_pushnumber(state,field==0?value.time:field==1?value.progress:field==2?value.normalized:value.weight);return 1;
    }
    static int SceneControllerLayerWrite(lua_State* state) {
        SceneWorld(state);const auto id=SceneId(state);const auto layer=ControllerLayerIndex(state,2,false);auto& self=Self(state);
        if(!self.animator_ || !self.animator_->Enabled())return luaL_error(state,"Controller command host unavailable");
        luaL_checktype(state,3,LUA_TNUMBER);
        const auto result=self.sceneCommands_.QueueLayerWeight(id,layer,lua_tonumber(state,3),self.animatorBatch_);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneControllerPlayState(lua_State* state) {
        SceneWorld(state);const auto id=SceneId(state);auto& self=Self(state);
        luaL_checktype(state,2,LUA_TSTRING);size_t bytes=0;const char* name=lua_tolstring(state,2,&bytes);
        double duration=0.2;if(!lua_isnoneornil(state,3)){luaL_checktype(state,3,LUA_TNUMBER);duration=lua_tonumber(state,3);}
        const auto layer=ControllerLayerIndex(state,4,true);
        if(!self.animator_ || !self.animator_->Enabled())return luaL_error(state,"Controller request host unavailable");
        const auto result=self.sceneCommands_.QueueForceState(id,layer,std::string_view(name,bytes),duration,self.animatorBatch_);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneControllerWrite(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);auto& self=Self(state);
        if(!self.animator_ || !self.animator_->Enabled())return luaL_error(state,"Controller command host unavailable");
        luaL_checktype(state,2,LUA_TSTRING);size_t bytes=0;const char* text=lua_tolstring(state,2,&bytes);
        const auto kind=static_cast<ScriptAnimatorRequestKind>(lua_tointeger(state,lua_upvalueindex(1)));
        double number=0;bool flag=false;
        if(kind==ScriptAnimatorRequestKind::Float){luaL_checktype(state,3,LUA_TNUMBER);number=lua_tonumber(state,3);}
        if(kind==ScriptAnimatorRequestKind::Bool){luaL_checktype(state,3,LUA_TBOOLEAN);flag=lua_toboolean(state,3)!=0;}
        const auto result=self.sceneCommands_.QueueController(id,kind,std::string_view(text,bytes),number,flag,self.animatorBatch_);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneSetAnimator(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);
        luaL_checktype(state,2,LUA_TSTRING);luaL_checktype(state,3,LUA_TNUMBER);
        luaL_checktype(state,4,LUA_TBOOLEAN);luaL_checktype(state,5,LUA_TBOOLEAN);
        size_t length=0;const char* clip=lua_tolstring(state,2,&length);
        // Only borrowed/trivial values span luaL_error (Lua's longjmp).
        const auto result=Self(state).sceneCommands_.SetAnimator(id,std::string_view(clip,length),lua_tonumber(state,3),lua_toboolean(state,4)!=0,lua_toboolean(state,5)!=0);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneRemoveAnimator(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);const auto result=Self(state).sceneCommands_.RemoveAnimator(id);
        SceneStructuralError(state,result);lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneChange(lua_State* state) {
        SceneWorld(state);
        luaL_checktype(state,1,LUA_TSTRING);
        size_t length=0;
        const char* path=lua_tolstring(state,1,&length);
        const auto delay=luaL_optnumber(state,2,0);
        auto& self=Self(state);
        const std::string_view destination(path,length);
        const bool allowed=self.current_ && !self.invokingDestroy_ && self.transitions_ &&
            self.transitions_->Available() && !self.transition_ &&
            std::isfinite(delay) && delay>=0 && delay<=10 &&
            !destination.empty() && ValidScriptText(destination,4096);
        if (allowed) {
            try {self.transition_.emplace(ScriptSceneTransition{std::string(destination),delay});}
            catch (...) {return luaL_error(state,"Cannot stage scene transition");}
        }
        lua_pushboolean(state,allowed);
        return 1;
    }
    static int SceneSpawn(lua_State* state){
        SceneWorld(state);luaL_checktype(state,1,LUA_TSTRING);
        size_t length=0;const char* name=lua_tolstring(state,1,&length);
        const auto parent=lua_isnoneornil(state,2) ? 0 : SceneId(state,2,true);
        Scene::EntityId created=0;
        const auto result=Self(state).sceneCommands_.Spawn(std::string_view(name,length),parent,created);
        SceneStructuralError(state,result);
        if(result==ScriptSceneCommands::Result::Success)lua_pushinteger(state,static_cast<lua_Integer>(created));else lua_pushnil(state);
        return 1;
    }
    static int SceneSpawnModel(lua_State* state){
        SceneWorld(state);luaL_checktype(state,1,LUA_TSTRING);luaL_checktype(state,2,LUA_TSTRING);
        size_t nameLength=0,pathLength=0,guidLength=0;
        const char* name=lua_tolstring(state,1,&nameLength);const char* path=lua_tolstring(state,2,&pathLength);
        const auto parent=lua_isnoneornil(state,3) ? 0 : SceneId(state,3,true);
        const char* guid="";if(!lua_isnoneornil(state,4)){luaL_checktype(state,4,LUA_TSTRING);guid=lua_tolstring(state,4,&guidLength);}
        Scene::EntityId created=0;
        const auto result=Self(state).sceneCommands_.SpawnModel(std::string_view(name,nameLength),std::string_view(path,pathLength),std::string_view(guid,guidLength),parent,created);
        SceneStructuralError(state,result);
        if(result==ScriptSceneCommands::Result::Success)lua_pushinteger(state,static_cast<lua_Integer>(created));else lua_pushnil(state);
        return 1;
    }
    static int SceneSpawnPrefab(lua_State* state){
        SceneWorld(state);luaL_checktype(state,1,LUA_TSTRING);size_t pathLength=0,guidLength=0;
        const char* path=lua_tolstring(state,1,&pathLength);const auto parent=lua_isnoneornil(state,2)?0:SceneId(state,2,true);
        const char* guid="";if(!lua_isnoneornil(state,3)){luaL_checktype(state,3,LUA_TSTRING);guid=lua_tolstring(state,3,&guidLength);}
        Scene::EntityId created=0;const auto result=Self(state).sceneCommands_.SpawnPrefab(std::string_view(path,pathLength),std::string_view(guid,guidLength),parent,created);
        SceneStructuralError(state,result);if(result==ScriptSceneCommands::Result::Success)lua_pushinteger(state,static_cast<lua_Integer>(created));else lua_pushnil(state);return 1;
    }
    static int SceneClone(lua_State* state){
        SceneWorld(state);const auto source=SceneId(state);
        const auto parent=lua_isnoneornil(state,2) ? 0 : SceneId(state,2,true);
        Scene::EntityId created=0;
        const auto result=Self(state).sceneCommands_.Clone(source,parent,created);
        SceneStructuralError(state,result);
        if(result==ScriptSceneCommands::Result::Success)lua_pushinteger(state,static_cast<lua_Integer>(created));else lua_pushnil(state);
        return 1;
    }
    static int SceneDestroy(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);
        const bool subtree=lua_toboolean(state,lua_upvalueindex(1))!=0;
        const auto result=Self(state).sceneCommands_.Destroy(id,subtree);
        SceneStructuralError(state,result);
        lua_pushboolean(state,result==ScriptSceneCommands::Result::Success);return 1;
    }
    static int SceneTransform(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);const auto* transform=Self(state).sceneCommands_.Transform(id);if(!transform){lua_pushnil(state);return 1;}
        const int field=static_cast<int>(lua_tointeger(state,lua_upvalueindex(1)));
        return PushVector(state,field==0 ? transform->position : field==1 ? transform->rotation : transform->scale);
    }
    static int SceneWorldTransform(lua_State* state){
        SceneWorld(state);const auto id=SceneId(state);
        Matrix4x4 matrix{};
        const bool found=Self(state).sceneCommands_.WorldMatrix(id,matrix);
        if(!found){lua_pushnil(state);return 1;}
        for(const auto& row:matrix.m)for(float value:row)if(!std::isfinite(value)){lua_pushnil(state);return 1;}
        if(lua_toboolean(state,lua_upvalueindex(1))==0)return PushVector(state,{matrix.m[3][0],matrix.m[3][1],matrix.m[3][2]});
        // Only trivial native values span the allocating Lua API. The bounded
        // table belongs to the VM and participates in its allocator budget.
        lua_createtable(state,16,0);
        for(int row=0;row<4;++row)for(int column=0;column<4;++column){lua_pushnumber(state,matrix.m[row][column]);lua_rawseti(state,-2,row*4+column+1);}
        return 1;
    }
    static int InputPadConnection(lua_State* state) {
        const auto& frame=Input(state);
        const int field=static_cast<int>(lua_tointeger(state,lua_upvalueindex(1)));
        lua_pushboolean(state,frame.available && (field==0 ? frame.pad.connected : field==1 ? frame.pad.connectedThisFrame : frame.pad.disconnectedThisFrame));return 1;
    }
    static bool InputPadRight(lua_State* state) {
        if(lua_isnoneornil(state,1))return false;
        luaL_checktype(state,1,LUA_TBOOLEAN);return lua_toboolean(state,1)!=0;
    }
    static int InputPadStick(lua_State* state) {
        const auto& frame=Input(state);const bool right=InputPadRight(state);
        const bool enabled=frame.available && frame.pad.connected;
        lua_pushnumber(state,enabled ? (right ? frame.pad.rightX : frame.pad.leftX) : 0);
        lua_pushnumber(state,enabled ? (right ? frame.pad.rightY : frame.pad.leftY) : 0);return 2;
    }
    static int InputPadTrigger(lua_State* state) {
        const auto& frame=Input(state);const bool right=InputPadRight(state);
        lua_pushnumber(state,frame.available && frame.pad.connected ? (right ? frame.pad.rightTrigger : frame.pad.leftTrigger) : 0);return 1;
    }
    static int AudioAvailable(lua_State* state) {
        const auto& self=Self(state);lua_pushboolean(state,self.current_ && self.audio_ && self.audio_->Enabled());return 1;
    }
    static float AudioVolume(lua_State* state,int index) {
        const auto value=luaL_checknumber(state,index);
        // Validate Lua's double before narrowing: tiny negatives and values just
        // above one can otherwise round into the valid float range.
        if(!std::isfinite(value) || value<0 || value>1) luaL_error(state,"Audio volume must be between zero and one");
        return static_cast<float>(value);
    }
    static int AudioRequest(lua_State* state) {
        auto& self=Self(state);
        if(!self.current_ || !self.audio_ || !self.audio_->Enabled()) return luaL_error(state,"Audio host is unavailable outside an enabled script hook");
        if(self.audioBatch_.size()>=ScriptAudioRequests::kMaxPerHook) return luaL_error(state,"Audio hook request budget exceeded");
        const auto operation=static_cast<ScriptAudioRequest::Operation>(lua_tointeger(state,lua_upvalueindex(1)));
        const bool stop=operation==ScriptAudioRequest::Operation::StopAll;
        const char* path=nullptr;size_t size=0;bool loop=false;float volume=1;
        if(!stop) {
            luaL_checktype(state,1,LUA_TSTRING);path=lua_tolstring(state,1,&size);
            if(!ValidScriptText(std::string_view(path,size),4096,true)) return luaL_error(state,"Invalid audio path");
            if(operation==ScriptAudioRequest::Operation::SetVolume) volume=AudioVolume(state,2);
            else {
                if(!lua_isnoneornil(state,2)) {luaL_checktype(state,2,LUA_TBOOLEAN);loop=lua_toboolean(state,2)!=0;}
                if(!lua_isnoneornil(state,3)) volume=AudioVolume(state,3);
            }
            if(volume<0 || volume>1) return luaL_error(state,"Audio volume must be between zero and one");
        }
        // Lua errors longjmp: construct/destroy C++ strings only in this scope,
        // and never invoke Lua APIs from within it.
        bool failed=false,invalid=false;
        try {
            ScriptAudioRequest request;request.entity=self.current_->id;
            request.owner=self.audioOwner_;
            request.operation=operation;
            if(!stop) {
                request.path.assign(path,size);request.loop=loop;request.volume=volume;
                const auto native=std::filesystem::path(std::u8string(request.path.begin(),request.path.end()));
                invalid=native.has_root_name() || native.has_root_directory() || request.path.find(':')!=std::string::npos;
                for(const auto& part:native) if(part=="..") invalid=true;
            }
            if(!invalid) self.audioBatch_.push_back(std::move(request));
        } catch(...) {failed=true;}
        if(invalid) return luaL_error(state,"Audio path must be project-relative without parent traversal");
        if(failed) return luaL_error(state,"Cannot allocate audio request");
        return 0;
    }
    static void InitializeAudio(lua_State* state) {
        lua_newtable(state);lua_pushcfunction(state,AudioAvailable);lua_setfield(state,-2,"available");
        lua_pushinteger(state,static_cast<int>(ScriptAudioRequest::Operation::Play));lua_pushcclosure(state,AudioRequest,1);lua_setfield(state,-2,"play");
        lua_pushinteger(state,static_cast<int>(ScriptAudioRequest::Operation::StopAll));lua_pushcclosure(state,AudioRequest,1);lua_setfield(state,-2,"stop_all");
        lua_pushinteger(state,static_cast<int>(ScriptAudioRequest::Operation::SetVolume));lua_pushcclosure(state,AudioRequest,1);lua_setfield(state,-2,"set_volume");
        lua_setglobal(state,"audio");
    }
    static void InitializeInput(lua_State* state) {
        lua_newtable(state);
        const luaL_Reg methods[]={{"available",InputAvailable},{"action_axis",InputAxis},{"mouse_position",InputMousePosition},{"mouse_delta",InputMouseDelta},{"mouse_wheel",InputWheel},{"pad_stick",InputPadStick},{"pad_trigger",InputPadTrigger},{nullptr,nullptr}};
        luaL_setfuncs(state,methods,0);
        const char* names[4][3]={{"key_down","key_pressed","key_released"},{"mouse_down","mouse_pressed","mouse_released"},{"action_down","action_pressed","action_released"},{"pad_down","pad_pressed","pad_released"}};
        for(int domain=0;domain<4;++domain) for(int field=0;field<3;++field) {
            lua_pushinteger(state,domain);lua_pushinteger(state,field);lua_pushcclosure(state,InputButton,2);lua_setfield(state,-2,names[domain][field]);
        }
        const char* connectionNames[]={"pad_connected","pad_connected_this_frame","pad_disconnected_this_frame"};
        for(int field=0;field<3;++field){lua_pushinteger(state,field);lua_pushcclosure(state,InputPadConnection,1);lua_setfield(state,-2,connectionNames[field]);}
        lua_newtable(state);
        const char* padNames[]={"DpadUp","DpadDown","DpadLeft","DpadRight","Start","Back","LeftStick","RightStick","LeftShoulder","RightShoulder","A","B","X","Y"};
        static_assert(sizeof(padNames)/sizeof(padNames[0])==static_cast<size_t>(InputSystem::GamepadButton::Count));
        for(size_t index=0;index<static_cast<size_t>(InputSystem::GamepadButton::Count);++index){lua_pushinteger(state,static_cast<lua_Integer>(index));lua_setfield(state,-2,padNames[index]);}
        lua_setfield(state,-2,"pad_buttons");
        lua_newtable(state);
        // DirectInput keyboard scan codes, not virtual-key codes or text input.
        struct Key { const char* name;int code; };
        const Key keys[]={{"W",0x11},{"A",0x1e},{"S",0x1f},{"D",0x20},{"Q",0x10},{"E",0x12},{"Space",0x39},{"Escape",0x01},{"Enter",0x1c},{"LeftShift",0x2a},{"LeftControl",0x1d},{"Up",0xc8},{"Down",0xd0},{"Left",0xcb},{"Right",0xcd}};
        for(const auto& key:keys) { lua_pushinteger(state,key.code);lua_setfield(state,-2,key.name); }
        lua_setfield(state,-2,"keys");lua_setglobal(state,"input");
    }
    static int CaptureHooks(lua_State* state) {
        auto& self=Self(state);
        lua_pushglobaltable(state);
        for(size_t index=0;index<self.hooks_.size();++index) {
            lua_pushstring(state,hookNames_[index]);lua_rawget(state,-2);
            if(lua_isnil(state,-1)) { lua_pop(state,1);continue; }
            if(!lua_isfunction(state,-1)) return luaL_error(state,"%s must be a function",hookNames_[index]);
            self.hooks_[index]=luaL_ref(state,LUA_REGISTRYINDEX);
        }
        if(self.hooks_[1]==LUA_NOREF && self.hooks_[3]==LUA_NOREF &&
            self.hooks_[4]==LUA_NOREF && self.hooks_[5]==LUA_NOREF)
            return luaL_error(state,"Lua script must define an update or trigger hook");
        lua_pop(state,1);return 0;
    }
    static int Initialize(lua_State* state) {
        // Trusted-project isolation, not an OS security sandbox. No filesystem,
        // process, module loader, coroutine or debug library is exposed.
        const luaL_Reg libraries[]={{"_G",luaopen_base},{LUA_MATHLIBNAME,luaopen_math},{LUA_STRLIBNAME,luaopen_string},{LUA_TABLIBNAME,luaopen_table},{LUA_UTF8LIBNAME,luaopen_utf8},{nullptr,nullptr}};
        for(const auto* library=libraries;library->func;++library) { luaL_requiref(state,library->name,library->func,1);lua_pop(state,1); }
        // Prevent scripts from catching/retrying quota errors or loading bytecode.
        // No user finalizers: lua_close must not execute an unbounded __gc hook.
        for(const char* name : {"dofile","loadfile","load","collectgarbage","pcall","xpcall","print","setmetatable"}) { lua_pushnil(state);lua_setglobal(state,name); }
        lua_getglobal(state,"string");lua_pushnil(state);lua_setfield(state,-2,"dump");lua_pop(state,1);
        const luaL_Reg methods[]={{"id",Id},{"property",Property},{"position",Position},{"rotation",Rotation},{"scale",Scale},{"set_position",SetPosition},{"set_rotation",SetRotation},{"set_scale",SetScale},{"translate",Translate},{"velocity",Velocity},{"set_velocity",SetVelocity},{"active",Active},{"set_active",SetActive},{nullptr,nullptr}};
        luaL_newmetatable(state,entityType);luaL_setfuncs(state,methods,0);lua_pushvalue(state,-1);lua_setfield(state,-2,"__index");
        lua_pushliteral(state,"Kubo.Entity");lua_setfield(state,-2,"__metatable");lua_pop(state,1);
        *static_cast<LuaInstance**>(lua_newuserdatauv(state,sizeof(LuaInstance*),0))=&Self(state);
        luaL_setmetatable(state,entityType);Self(state).entityReference_=luaL_ref(state,LUA_REGISTRYINDEX);
        InitializeInput(state);
        lua_newtable(state);
        const luaL_Reg sceneMethods[]={{"find",SceneFind},{"entity_count",SceneCount},{"exists",SceneExists},{"name",SceneName},{"parent",SceneParent},{"active",SceneActive},{"velocity",SceneVelocity},{"animator",SceneAnimator},{"animator_state",SceneAnimatorState},{"controller_state",SceneControllerState},{"set_animator",SceneSetAnimator},{"remove_animator",SceneRemoveAnimator},{nullptr,nullptr}};
        luaL_setfuncs(state,sceneMethods,0);
        lua_pushcfunction(state,SceneLight);lua_setfield(state,-2,"light");
        lua_pushcfunction(state,SceneMaterial);lua_setfield(state,-2,"material");
        lua_pushcfunction(state,SceneCanvas);lua_setfield(state,-2,"canvas");
        lua_pushcfunction(state,SceneSprite);lua_setfield(state,-2,"sprite");
        lua_pushcfunction(state,SceneSetCanvas);lua_setfield(state,-2,"set_canvas");
        lua_pushcfunction(state,SceneSetSprite);lua_setfield(state,-2,"set_sprite");
        lua_pushboolean(state,false);lua_pushcclosure(state,SceneRemoveUI,1);lua_setfield(state,-2,"remove_canvas");
        lua_pushboolean(state,true);lua_pushcclosure(state,SceneRemoveUI,1);lua_setfield(state,-2,"remove_sprite");
        lua_pushcfunction(state,SceneSetLight);lua_setfield(state,-2,"set_light");
        lua_pushcfunction(state,SceneSetMaterial);lua_setfield(state,-2,"set_material");
        lua_pushboolean(state,false);lua_pushcclosure(state,SceneRemoveRender,1);lua_setfield(state,-2,"remove_light");
        lua_pushboolean(state,true);lua_pushcclosure(state,SceneRemoveRender,1);lua_setfield(state,-2,"remove_material");
        lua_pushcfunction(state,SceneCollider);lua_setfield(state,-2,"collider");
        lua_pushcfunction(state,SceneColliderShape);lua_setfield(state,-2,"collider_shape");
        lua_pushcfunction(state,SceneRaycast);lua_setfield(state,-2,"raycast");
        lua_pushcfunction(state,SceneOverlapBox);lua_setfield(state,-2,"overlap_box");
        lua_pushcfunction(state,SceneRigidBody);lua_setfield(state,-2,"rigid_body");
        lua_pushcfunction(state,SceneSetCollider);lua_setfield(state,-2,"set_collider");
        lua_pushcfunction(state,SceneSetRoundCollider);lua_setfield(state,-2,"set_round_collider");
        lua_pushcfunction(state,SceneSetRigidBody);lua_setfield(state,-2,"set_rigid_body");
        lua_pushboolean(state,false);lua_pushcclosure(state,SceneRemovePhysics,1);lua_setfield(state,-2,"remove_collider");
        lua_pushboolean(state,true);lua_pushcclosure(state,SceneRemovePhysics,1);lua_setfield(state,-2,"remove_rigid_body");
        const char* animatorCommands[]={"animator_play","animator_pause","animator_stop","animator_restart"};
        for(int i=0;i<4;++i){lua_pushinteger(state,i);lua_pushcclosure(state,SceneAnimatorCommand,1);lua_setfield(state,-2,animatorCommands[i]);}
        const char* controllerCommands[]={"controller_set_float","controller_set_bool","controller_trigger"};
        for(int i=0;i<3;++i){lua_pushinteger(state,i+1);lua_pushcclosure(state,SceneControllerWrite,1);lua_setfield(state,-2,controllerCommands[i]);}
        const char* layerValues[]={"controller_time","controller_progress","controller_normalized_time","controller_layer_weight"};
        for(int i=0;i<4;++i){lua_pushinteger(state,i);lua_pushcclosure(state,SceneControllerLayerValue,1);lua_setfield(state,-2,layerValues[i]);}
        lua_pushcfunction(state,SceneControllerLayerWrite);lua_setfield(state,-2,"controller_set_layer_weight");
        lua_pushcfunction(state,SceneControllerPlayState);lua_setfield(state,-2,"controller_play_state");
        lua_pushcfunction(state,SceneSpawn);lua_setfield(state,-2,"spawn");
        lua_pushcfunction(state,SceneSpawnModel);lua_setfield(state,-2,"spawn_model");
        lua_pushcfunction(state,SceneSpawnPrefab);lua_setfield(state,-2,"spawn_prefab");
        lua_pushcfunction(state,SceneChange);lua_setfield(state,-2,"change_scene");
        lua_pushcfunction(state,SceneClone);lua_setfield(state,-2,"clone");
        lua_pushboolean(state,false);lua_pushcclosure(state,SceneDestroy,1);lua_setfield(state,-2,"destroy");
        lua_pushboolean(state,true);lua_pushcclosure(state,SceneDestroy,1);lua_setfield(state,-2,"destroy_subtree");
        const char* transformNames[]={"position","rotation","scale"};
        for(int field=0;field<3;++field){lua_pushinteger(state,field);lua_pushcclosure(state,SceneTransform,1);lua_setfield(state,-2,transformNames[field]);}
        lua_pushboolean(state,0);lua_pushcclosure(state,SceneWorldTransform,1);lua_setfield(state,-2,"world_position");
        lua_pushboolean(state,1);lua_pushcclosure(state,SceneWorldTransform,1);lua_setfield(state,-2,"world_matrix");
        const char* commandNames[]={"set_position","translate","set_rotation","set_scale","set_active","set_velocity"};
        for(int operation=0;operation<6;++operation){lua_pushinteger(state,operation);lua_pushcclosure(state,SceneCommand,1);lua_setfield(state,-2,commandNames[operation]);}
        lua_setglobal(state,"scene");
        InitializeAudio(state);
        return 0;
    }
    StateLease lease_;
    lua_State* state_=nullptr;
    size_t used_=0;
    int remaining_=0,entityReference_=LUA_NOREF;
    Scene::EntityRecord* current_=nullptr;
    std::shared_ptr<const InputSystem::InputFrame> input_;
    std::shared_ptr<ScriptAudioRequests> audio_;
    std::shared_ptr<ScriptAnimatorRequests> animator_;
    ScriptAnimatorBatch animatorBatch_;
    std::shared_ptr<const ScriptSceneView> scene_;
    std::shared_ptr<ScriptSceneTransitions> transitions_;
    std::optional<ScriptSceneTransition> transition_;
    bool invokingDestroy_ = false;
    std::shared_ptr<ScriptAudioOwner> audioOwner_=std::make_shared<ScriptAudioOwner>();
    std::vector<ScriptAudioRequest> audioBatch_;
    ScriptSceneCommands sceneCommands_;
    std::vector<Scene::EntityId> queryHits_;
    ScriptProperties defaults_;
    std::array<int,6> hooks_{LUA_NOREF,LUA_NOREF,LUA_NOREF,LUA_NOREF,LUA_NOREF,LUA_NOREF};
    static constexpr std::array<const char*,6> hookNames_{
        "on_create","on_update","on_destroy","on_fixed_update","on_trigger_enter","on_trigger_exit"};
};
}
std::function<ScriptCallbacks()> CreateLuaScriptFactory(const std::string& source,const std::string& name,std::shared_ptr<const InputSystem::InputFrame> input,ScriptProperties& defaults,std::shared_ptr<ScriptAudioRequests> audio,std::shared_ptr<LuaResourceBudget> budget,std::shared_ptr<const ScriptSceneView> scene,std::shared_ptr<ScriptAnimatorRequests> animator,std::shared_ptr<ScriptSceneTransitions> transitions) {
    if(!budget)budget=std::make_shared<LuaResourceBudget>();
    auto retained=std::make_shared<LuaSourceLease>(source,budget);
    struct Seed { std::shared_ptr<LuaInstance> value; };
    auto seed=std::make_shared<Seed>();seed->value=std::make_shared<LuaInstance>(retained->Bytes(),name,input,audio,budget,scene,animator,transitions);
    defaults=seed->value->Defaults();
    return [seed,retained,name,input,defaults,audio,budget,scene,animator,transitions]() {
        auto state=std::exchange(seed->value,nullptr);
        if(!state) state=std::make_shared<LuaInstance>(retained->Bytes(),name,input,audio,budget,scene,animator,transitions);
        state->SetDefaults(defaults);
        ScriptCallbacks callbacks;
        if(state->HasHook(0))callbacks.onCreate=[state](Scene::EntityRecord& entity){state->Call(0,entity);};
        if(state->HasHook(1))callbacks.onUpdate=[state](Scene::EntityRecord& entity,float dt){state->Call(1,entity,dt);};
        if(state->HasHook(2))callbacks.onDestroy=[state](Scene::EntityRecord& entity){state->Call(2,entity);};
        if(state->HasHook(3))callbacks.onFixedUpdate=[state](Scene::EntityRecord& entity,float dt){state->Call(3,entity,dt);};
        if(state->HasHook(4))callbacks.onTriggerEnter=[state](Scene::EntityRecord& entity,Scene::EntityId other){state->Call(4,entity,0,other);};
        if(state->HasHook(5))callbacks.onTriggerExit=[state](Scene::EntityRecord& entity,Scene::EntityId other){state->Call(5,entity,0,other);};
        return callbacks;
    };
}
}
