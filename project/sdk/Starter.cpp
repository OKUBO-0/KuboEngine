#include "engine/app/RuntimeGame.h"
#include "engine/base/core/RunDiagnostics.h"
#include "engine/base/ProjectContext.h"
#include "engine/base/core/WinApp.h"
#include "engine/script/LuaScript.h"
#include "engine/script/ScriptSceneCommands.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/core/SceneManager.h"
#include <Windows.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <cmath>
#include <algorithm>

// An external game derives from the prebuilt RuntimeGame, not engine source.
class StarterGame final : public Engine::App::RuntimeGame {
public:
    // Framework::Run owns shutdown/retry while this complete host remains live.
    void RunSafely() {Run();}
    void Update() override {
        RuntimeGame::Update();
        // Add native game logic here; generic Scene/Lua/Physics still run normally.
        ++frames_;
        if(verify_){const auto usage=GetPhysicsAdvanceUsage();physicsTicks_+=usage.steps;if(usage.steps>Engine::Physics::PhysicsWorld::MaximumFrameSteps)throw std::runtime_error("SDK fixed physics frame budget exceeded");
            if(GetFixedInputUsage().pendingSamples>Engine::InputSystem::FixedInputBuffer::MaximumPendingSamples)throw std::runtime_error("SDK fixed input queue exceeded sample cap");}
        if(verify_ && frames_==8) {
            if(!physicsTicks_)throw std::runtime_error("SDK RuntimeGame did not use fixed physics scheduler");
            VerifyLuaResourceBudget(GetProjectRoot()/"Assets/move.lua");
            VerifyModelGeneration();
            const auto* mover=GetWorld().Find(2);
            const auto* camera=GetWorld().Find(1);
            if(!camera || camera->transform.rotation.y!=0.0625f)throw std::runtime_error("SDK RuntimeGame did not publish Lua peer Camera rotation");
            size_t spawned=0;for(const auto& record:GetWorld().GetEntities()){
                if(record.name=="SDK runtime temporary" || record.name=="SDK runtime temporary child")throw std::runtime_error("SDK RuntimeGame retained deleted Lua subtree");
                if(record.name=="SDK runtime spawned"){++spawned;if(record.parent!=1 || record.transform.position.x!=4)throw std::runtime_error("SDK RuntimeGame spawned Entity parent/value mismatch");}
            }
            if(spawned!=1)throw std::runtime_error("SDK RuntimeGame did not retain one Lua-spawned Entity");
            if(!mover || std::abs(mover->transform.position.y-double(physicsTicks_)*Engine::Physics::PhysicsWorld::FixedStepSeconds)>0.0001)
                throw std::runtime_error("SDK RuntimeGame did not dispatch authored fixed Lua hooks per physics tick");
            size_t clones=0;for(const auto& record:GetWorld().GetEntities())if(record.id!=2 && record.name=="Native SDK Lua/Audio/model"){
                ++clones;if(!record.hasModel || !record.hasScript || !record.script.started || !record.hasAudio || record.transform.position.x<=10 || record.parent || !record.prefabLink.is_null())
                    throw std::runtime_error("SDK RuntimeGame component clone did not retain data/start fresh Script");
            }
            if(clones!=1)throw std::runtime_error("SDK RuntimeGame component clone missing/multiplied");
            size_t models=0;const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            for(const auto& record:GetWorld().GetEntities())if(record.name=="SDK runtime Model"){
                ++models;if(!record.hasModel || record.model.assetPath!="Assets/triangle.obj" || record.model.assetGuid!="0123456789abcdef0123456789abcdef" || record.transform.position.x!=-2 || record.transform.scale.x!=0.5f || record.parent || !scene || !scene->GetModelRenderStatus(record.id).loaded)
                    throw std::runtime_error("SDK RuntimeGame Model generation/asset identity/renderer mismatch");
            }
            if(models!=1)throw std::runtime_error("SDK RuntimeGame Model generation missing/multiplied");
            if(!mover || mover->transform.position.x<=0 || GetScriptAudioHost().VoiceCount()!=2 || GetEntityAudioHost().VoiceCount()!=2)
                throw std::runtime_error("SDK consumer did not run authored Lua/component Audio");
            if(verifyMaterials_) {
                const auto* body=GetWorld().Find(4);
                if(!body || body->collider.friction!=1 || body->collider.restitution!=1 ||
                    std::abs(body->rigidBody.velocity.y-2)>0.001f || std::abs(body->rigidBody.velocity.x)>0.001f || body->rigidBody.grounded)
                    throw std::runtime_error("SDK consumer did not restore/simulate authored friction/bounce");
            }
            if(verifyHierarchy_) {
                const auto* body=GetWorld().Find(4);Matrix4x4 matrix{};
                if(!body || body->parent!=5 || !GetWorld().GetWorldMatrix(4,matrix) || matrix.m[3][1]<1.49f ||
                    std::abs(matrix.m[3][0]-1000)>0.2f || body->transform.position.x>=-0.74f)
                    throw std::runtime_error("SDK consumer did not simulate parent-composed physics");
            }
            OutputDebugStringA("SDK consumer rendered authored Scene\n");
            PostMessageW(winApp->GetHwnd(),WM_CLOSE,0,0);
        }
    }
    void Draw() override {RuntimeGame::Draw();++draws_;}
    void Finalize() override {
        RuntimeGame::Finalize();
        // Post-shutdown verification can fail too. Report it once so a retry
        // can confirm retirement instead of manufacturing a persistent fault.
        if(verificationFinalized_)return;
        verificationFinalized_=true;
        if(verify_ && (draws_<8 || GetScriptAudioHost().VoiceCount()!=0 || GetEntityAudioHost().VoiceCount()!=0 || GetEntityAudioHost().BufferBytes()!=0))
            throw std::runtime_error("SDK consumer render/cleanup verification failed");
    }
    void SetVerify(bool verify,bool materials=false,bool hierarchy=false) {
        verify_=verify;verifyMaterials_=materials;verifyHierarchy_=hierarchy;
        if(verify_) {VerifyKeepWorld();VerifyPadInput();VerifySceneView();VerifySceneCommands();VerifySceneClone();}
    }
private:
    void VerifyModelGeneration() {
        using namespace Engine::Scripting;using namespace Engine::Scene;
        Engine::Assets::AssetRegistry registry;std::string error;
        if(!registry.Scan(GetProjectRoot()/"Assets",error,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
        ScriptAssetView assets(registry,GetProjectRoot());EntityWorld world;world.CreateEntity("SDK Model owner");auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory("function on_update(e,dt) e:translate(2,0,0);local id=scene.spawn_model('SDK Model','Assets/old.obj',e:id(),'0123456789abcdef0123456789abcdef');assert(id==2 and scene.set_position(id,3,0,0) and scene.world_position(id)==5) end","SDK Model creation",{},defaults,{}, {},view);
        auto callbacks=factory();{ScriptSceneView::WriteScope scope(*view,world,&assets);callbacks.onUpdate(*world.Find(1),0);}
        if(!world.Find(2) || !world.Find(2)->hasModel || world.Find(2)->model.assetPath!="Assets/triangle.obj" || world.Find(2)->model.assetGuid!="0123456789abcdef0123456789abcdef")throw std::runtime_error("SDK Lua Model/header/archive mismatch");
        auto detached=*world.Find(1);ScriptSceneCommands commands;EntityId born=0;
        {ScriptSceneView::WriteScope scope(*view,world,&assets);commands.Begin(view.get(),detached);if(commands.SpawnModel("SDK native Model","Assets/triangle.obj","",0,born)!=ScriptSceneCommands::Result::Success || born!=3 || commands.CreatedCount()!=1 || !commands.Prepare() || !commands.PublishPrepared())throw std::runtime_error("SDK native Model/header/archive mismatch");}
        const auto snapshot=world.Capture().dump();const auto high=world.AllocationHighWatermark();
        auto failure=CreateLuaScriptFactory("function on_update(e,dt) e:translate(9,0,0);assert(scene.spawn_model('Discarded','Assets/triangle.obj'));scene.spawn_model('Wrong','Assets/move.lua') end","SDK Model rollback",{},defaults,{}, {},view);callbacks=failure();bool refused=false;
        {ScriptSceneView::WriteScope scope(*view,world,&assets);try{callbacks.onUpdate(*world.Find(1),0);}catch(const LuaScriptError&){refused=true;}}
        if(!refused || world.Capture().dump()!=snapshot || world.AllocationHighWatermark()!=high)throw std::runtime_error("SDK Model rejection leaked candidate/self/ID");
    }
    static void VerifySceneClone() {
        using namespace Engine::Scripting;using namespace Engine::Scene;
        EntityWorld world;const auto source=world.CreateEntity("SDK clone template");world.CreateEntity("SDK clone child",source);const auto owner=world.CreateEntity("SDK clone owner");
        if(!world.SetModel(source,"Assets/triangle.obj") || !world.SetCamera(source,{}) || !world.SetCollider(source,{}) || !world.SetRigidBody(source,{{4,5,6},3,true,false,true}) || !world.SetScript(source,"Assets/move.lua") || !world.SetScriptProperty(source,"label",std::string("日本語")))throw std::runtime_error("SDK clone fixture");
        Engine::AudioSystem::AudioComponent audio;audio.assetPath="Assets/sound.wav";audio.loop=true;if(!world.SetAudio(source,audio))throw std::runtime_error("SDK clone Audio fixture");world.Find(source)->script.started=true;
        auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.translate(1,2,0,0));local copy=scene.clone(1,e:id());assert(copy==4 and scene.parent(5)==copy and scene.position(copy)==2 and scene.clone(999)==nil) end","SDK component clone",{},defaults,{}, {},view);
        auto callbacks=factory();{ScriptSceneView::WriteScope scope(*view,world);callbacks.onUpdate(*world.Find(owner),0);}
        const auto* copy=world.Find(4);if(!copy || !copy->hasModel || !copy->hasCamera || !copy->hasCollider || !copy->hasRigidBody || copy->rigidBody.grounded || copy->rigidBody.velocity.x!=4 || !copy->hasScript || copy->script.started || copy->script.properties!=world.Find(1)->script.properties || !copy->hasAudio || copy->audio.assetPath!=audio.assetPath || !copy->prefabLink.is_null())throw std::runtime_error("SDK clone component/header/archive mismatch");
        auto detached=*world.Find(owner);ScriptSceneCommands commands;
        {ScriptSceneView::WriteScope scope(*view,world);commands.Begin(view.get(),detached);EntityId born=0;
            if(commands.Clone(4,0,born)!=ScriptSceneCommands::Result::Success || born!=6 || commands.CreatedCount()!=2 || !commands.Prepare() || !commands.PublishPrepared() || !world.Find(7) || world.Find(7)->parent!=born)throw std::runtime_error("SDK native clone/header/archive mismatch");}
        const auto snapshot=world.Capture().dump();const auto high=world.AllocationHighWatermark();
        auto failure=CreateLuaScriptFactory("function on_update(e,dt) e:translate(9,0,0);assert(scene.clone(1));error('discard clone') end","SDK clone rollback",{},defaults,{}, {},view);callbacks=failure();bool refused=false;
        {ScriptSceneView::WriteScope scope(*view,world);try{callbacks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
        if(!refused || world.Capture().dump()!=snapshot || world.AllocationHighWatermark()!=high)throw std::runtime_error("SDK clone error leaked self/components/IDs");
    }
    static void VerifySceneCommands() {
        using namespace Engine::Scripting;using namespace Engine::Scene;
        EntityWorld world;const auto self=world.CreateEntity("SDK command owner"),target=world.CreateEntity("SDK command target",self);world.SetRigidBody(target,{});
        auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.translate(e:id(),1,0,0) and scene.set_position(2,2,0,0) and scene.set_velocity(2,9,0,0));assert(scene.position(e:id())==1 and scene.world_matrix(2)[13]==3 and scene.velocity(2)==9) end","SDK commands",{},defaults,{}, {},view);
        auto callbacks=factory();{ScriptSceneView::WriteScope scope(*view,world);callbacks.onUpdate(*world.Find(self),0.01f);}
        if(world.Find(self)->transform.position.x!=1 || world.Find(target)->transform.position.x!=2 || world.Find(target)->rigidBody.velocity.x!=9 || view->World())throw std::runtime_error("SDK scoped peer command publication/header/archive mismatch");
        const EntityTransformOverride overrides[]={{self,{{10,0,0},{0,0,0},{1,1,1}}},{target,{{20,0,0},{0,0,0},{1,1,1}}}};Matrix4x4 matrix{};
        if(!world.GetWorldMatrix(target,matrix,overrides) || matrix.m[3][0]!=30 || world.Find(target)->transform.position.x!=2)throw std::runtime_error("SDK multi-override external link/read-only check failed");
        callbacks=factory();bool refused=false;{ScriptSceneView::Scope scope(*view,world);try{callbacks.onUpdate(*world.Find(self),0.01f);}catch(const LuaScriptError&){refused=true;}}
        if(!refused || world.Find(self)->transform.position.x!=1 || world.Find(target)->transform.position.x!=2)throw std::runtime_error("SDK readonly scope admitted Scene mutation");
        auto structural=CreateLuaScriptFactory("function on_update(e,dt) e:translate(2,0,0);local born=scene.spawn('SDK spawned',e:id());assert(born and scene.set_position(born,4,0,0) and scene.world_position(born)==7);assert(scene.destroy(2));assert(not scene.exists(2) and scene.parent(born)==e:id()) end","SDK structural commands",{},defaults,{}, {},view);
        callbacks=structural();{ScriptSceneView::WriteScope scope(*view,world);callbacks.onUpdate(*world.Find(self),0.01f);}
        if(world.GetEntities().size()!=2 || world.Find(target) || world.Find(self)->transform.position.x!=3 || !world.Find(3) || world.Find(3)->parent!=self || world.Find(3)->transform.position.x!=4)throw std::runtime_error("SDK structural hook/header/archive publication mismatch");
        auto detached=*world.Find(self);ScriptSceneCommands commands;
        {ScriptSceneView::WriteScope scope(*view,world);commands.Begin(view.get(),detached);EntityId born=0;
            if(commands.Spawn("SDK native command",0,born)!=ScriptSceneCommands::Result::Success ||
                commands.Apply(born,ScriptSceneCommands::Operation::Position,{5,0,0})!=ScriptSceneCommands::Result::Success ||
                !commands.Prepare() || !commands.PublishPrepared() || !world.Find(born) || world.Find(born)->transform.position.x!=5)
                throw std::runtime_error("SDK native structural candidate/header/archive mismatch");}
        const auto snapshot=world.Capture().dump();const auto allocator=world.AllocationHighWatermark();
        auto invalidName=CreateLuaScriptFactory("function on_update(e,dt) e:translate(9,0,0);assert(scene.translate(3,3,0,0));scene.spawn(string.char(255)) end","SDK invalid UTF8 spawn",{},defaults,{}, {},view);
        callbacks=invalidName();refused=false;
        {ScriptSceneView::WriteScope scope(*view,world);try{callbacks.onUpdate(*world.Find(self),0.01f);}catch(const LuaScriptError&){refused=true;}}
        if(!refused || world.Capture().dump()!=snapshot || world.AllocationHighWatermark()!=allocator)throw std::runtime_error("SDK invalid UTF8 admission did not roll back self/peer/allocator");
    }
    static void VerifySceneView() {
        using namespace Engine::Scripting;
        auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.entity_count()==1 and scene.find('SDK view')==e:id());e:translate(1,0,0);local x=scene.world_position(e:id());assert(x==1 and scene.world_matrix(e:id())[13]==1) end","SDK scoped view",{},defaults,{}, {},view);
        auto callbacks=factory();Engine::Scene::EntityWorld world;const auto id=world.CreateEntity("SDK view");
        {ScriptSceneView::Scope scope(*view,world);callbacks.onUpdate(*world.Find(id),0.01f);}
        if(view->World() || world.Find(id)->transform.position.x!=1)throw std::runtime_error("SDK scoped Scene lifetime/candidate read failed");
        auto candidate=*world.Find(id);candidate.transform.position.x=2;Matrix4x4 composed{};
        if(!world.GetWorldMatrix(id,composed,candidate) || composed.m[3][0]!=2 || world.Find(id)->transform.position.x!=1)throw std::runtime_error("SDK read-only TRS override overload/link failed");
        bool refused=false;try{callbacks.onUpdate(*world.Find(id),0.01f);}catch(const LuaScriptError&){refused=true;}
        if(!refused || world.Find(id)->transform.position.x!=1)throw std::runtime_error("SDK expired Scene view accepted or failed hook committed");
    }
    static void VerifyPadInput() {
        using namespace Engine::InputSystem;
        InputFrame frame;frame.available=true;
        GamepadSample sample;sample.connected=true;sample.buttons=0x1000;sample.leftX=-32768;sample.rightTrigger=255;
        frame.pad=MakeGamepadFrame(sample,{},true);
        if(frame.pad.leftX!=-1 || frame.pad.rightTrigger!=1 || !frame.pad.buttons[static_cast<size_t>(GamepadButton::A)].pressed)
            throw std::runtime_error("SDK pad snapshot conversion failed");
        ActionMap map;map.Bind("jump",10);map.BindPadButton("jump",GamepadButton::A,{2,0,0});map.BindPadAxis("move",ActionMap::PadAxis::LeftX,{3,0,0},0.5f);
        if(map.BindingCount("jump")!=2 || map.Evaluate("jump",[](int code){return code==10;},frame.pad).x!=3 || map.Evaluate("move",[](int){return false;},frame.pad).x!=-3)
            throw std::runtime_error("SDK mixed pad ActionMap header/archive mismatch");
        Engine::Scripting::ScriptRuntime runtime;Engine::Scene::EntityWorld world;
        runtime.SetInputFrame(frame);
        if(runtime.GetFixedInputUsage().pendingSamples!=1)throw std::runtime_error("SDK runtime missed pad-only transient (header/archive mismatch)");
        runtime.FixedUpdate(world,1.0f/60);
        if(runtime.GetFixedInputUsage().pendingSamples!=0)throw std::runtime_error("SDK fixed pad sample not consumed");
        runtime.SetInputFrame(frame);runtime.SetInputFrame({});
        if(runtime.GetFixedInputUsage().pendingSamples!=0)throw std::runtime_error("SDK unavailable pad input not flushed");
        runtime.Shutdown(world);runtime.Clear();
    }
    static void VerifyLuaResourceBudget(const std::filesystem::path& source) {
        using namespace Engine::Scripting;
        Engine::Assets::AssetRegistry quotaAssets;std::string quotaError;
        if(!quotaAssets.Scan(source.parent_path(),quotaError,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(quotaError);
        ScriptRuntime runtime(LuaResourceLimits{1024*1024,1});std::string error;
        runtime.SetAssetOwner(quotaAssets,source.parent_path().parent_path());
        runtime.SetAudioRequestsEnabled(true);
        if(!runtime.LoadResolvedAsset("SDK quota",source,error) || runtime.GetLuaResourceUsage().states!=1)
            throw std::runtime_error("SDK Lua seed quota failed: "+error);
        Engine::Scene::EntityWorld world;const auto first=world.CreateEntity("SDK quota first"),second=world.CreateEntity("SDK quota refused");
        world.SetScript(first,"SDK quota");world.SetScript(second,"SDK quota");runtime.Update(world,0.5f);
        if(world.Find(first)->transform.position.x!=0.5f || world.Find(second)->transform.position.x!=0 || runtime.GetLastError(second).find("VM budget")==std::string::npos || runtime.GetLuaResourceUsage().states!=1)
            throw std::runtime_error("SDK Lua aggregate VM refusal failed");
        runtime.Shutdown(world);runtime.Clear();
        if(runtime.GetLuaResourceUsage().states || runtime.GetLuaResourceUsage().memoryBytes)
            throw std::runtime_error("SDK Lua resource cleanup failed");
        ScriptRuntime frame(LuaResourceLimits{1024*1024,8,2});frame.SetAudioRequestsEnabled(true);
        frame.SetAssetOwner(quotaAssets,source.parent_path().parent_path());
        if(!frame.LoadResolvedAsset("SDK frame",source,error))throw std::runtime_error("SDK frame module validation failed: "+error);
        Engine::Scene::EntityWorld limited;const auto interrupted=limited.CreateEntity("SDK interrupted"),deferred=limited.CreateEntity("SDK deferred");
        limited.SetScript(interrupted,"SDK frame");limited.SetScript(deferred,"SDK frame");frame.Update(limited,0.5f);
        if(frame.GetLuaResourceUsage().instructions!=2 || !frame.GetLuaResourceUsage().instructionExhausted || frame.GetLuaResourceUsage().deferredEntities!=1 ||
            frame.GetLastError(interrupted).find("frame instruction")==std::string::npos || limited.Find(interrupted)->transform.position.x!=0 || limited.Find(deferred)->script.started || !frame.GetLastError(deferred).empty() || !frame.TakeAudioRequests().empty())
            throw std::runtime_error("SDK Lua frame refusal/deferral failed");
        frame.Shutdown(limited);frame.Clear();
        if(frame.GetLuaResourceUsage().states || frame.GetLuaResourceUsage().memoryBytes)throw std::runtime_error("SDK Lua frame cleanup failed");
        auto budget=std::make_shared<LuaResourceBudget>(LuaResourceLimits{800*1024,8});ScriptProperties defaults;
        const std::string padded="--"+std::string(512*1024,'x')+"\nfunction on_update(e,dt) end";
        auto factory=CreateLuaScriptFactory(padded,"SDK retained source",{},defaults,{},budget);
        const auto retained=budget->Usage();bool refused=false;
        try{auto extra=CreateLuaScriptFactory(padded,"SDK refused source",{},defaults,{},budget);}
        catch(const LuaScriptError& failure){refused=std::string(failure.what()).find("retained source memory budget")!=std::string::npos;}
        if(!refused || retained.memoryBytes<padded.size() || budget->Usage().memoryBytes!=retained.memoryBytes || budget->Usage().states!=retained.states)
            throw std::runtime_error("SDK retained Lua source refusal/accounting failed");
        auto shared=factory;factory={};
        if(budget->Usage().memoryBytes!=retained.memoryBytes)throw std::runtime_error("SDK shared Lua source ownership lost");
        shared={};if(budget->Usage().memoryBytes || budget->Usage().states)throw std::runtime_error("SDK retained source cleanup failed");
        ScriptRuntime diagnostics;
        if(diagnostics.LoadResolvedAsset("SDK diagnostics",source/"not-a-file.lua",error) || diagnostics.GetAssetLoadError("SDK diagnostics").empty() ||
            !ScriptRuntime::DescribeLoadFailure(17,"SDK diagnostics",error).starts_with("Entity 17 [SDK diagnostics]:"))
            throw std::runtime_error("SDK source load diagnostic API failed");
        diagnostics.Clear();if(!diagnostics.GetAssetLoadError("SDK diagnostics").empty())throw std::runtime_error("SDK source load diagnostic cleanup failed");
    }
    static void VerifyKeepWorld() {
        Engine::Scene::EntityWorld fixture;
        const auto parent=fixture.CreateEntity("SDK parent");
        const auto child=fixture.CreateEntity("SDK child",parent);
        fixture.Find(parent)->transform={{7,2,-5},{0.2f,0.3f,-0.4f},{-2,2,2}};
        fixture.Find(child)->transform={{1,2,3},{0.5f,1.7f,0.8f},{1,2,-3}};
        Matrix4x4 expected{},actual{};std::string error;
        if(!fixture.GetWorldMatrix(child,expected)) throw std::runtime_error("SDK fixture World unavailable");
        for(const auto target : {Engine::Scene::EntityId{0},parent}) {
            if(!fixture.SetParent(child,target,true,error) || !fixture.GetWorldMatrix(child,actual))
                throw std::runtime_error("SDK keep-World API failed: "+error);
            for(int row=0;row<4;++row) for(int column=0;column<4;++column)
                if(std::abs(double(expected.m[row][column])-actual.m[row][column])>0.001*(std::max)(1.0,std::abs(double(expected.m[row][column]))))
                    throw std::runtime_error("SDK keep-World API changed World pose");
        }
    }
    int frames_=0,draws_=0;bool verify_=false,verifyMaterials_=false,verifyHierarchy_=false,verificationFinalized_=false;
    size_t physicsTicks_=0;
};

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    try {
        std::filesystem::path manifest;bool verify=false,materials=false,hierarchy=false;
        int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);
        if(args) {for(int i=1;i<count;++i) {if(std::wstring(args[i])==L"--project" && i+1<count) manifest=args[++i];else if(std::wstring(args[i])==L"--sdk-verify") verify=true;else if(std::wstring(args[i])==L"--sdk-verify-materials") {verify=true;materials=true;}else if(std::wstring(args[i])==L"--sdk-verify-hierarchy") {verify=true;materials=true;hierarchy=true;}}LocalFree(args);}
        if(manifest.empty()) {
            std::wstring executable(32768,L'\0');
            const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
            if(!length || length>=executable.size()) throw std::runtime_error("Cannot resolve native game executable path");
            executable.resize(length);
            manifest=Engine::Base::ProjectContext::FindManifestUpwards(std::filesystem::path(executable));
        }
        if(manifest.empty()) throw std::runtime_error("No project manifest found; pass --project <project.kubo>");
        Engine::Base::ProjectContext project;std::string error;
        if(!project.Open(manifest,error)) throw std::runtime_error(error);
        auto game=std::make_unique<StarterGame>();game->SetVerify(verify,materials,hierarchy);game->SetProjectRoot(project.GetRootPath());game->SetGameOutputExtent(project.GetSettings().gameOutput);
        if(!game->ConfigureLuaResourceLimits(project.GetSettings().luaLimits,error))throw std::runtime_error(error);
        if(verify){const auto applied=game->GetLuaResourceLimits(),expected=project.GetSettings().luaLimits;
            if(applied.memoryBytes!=expected.memoryBytes || applied.states!=expected.states || applied.frameInstructions!=expected.frameInstructions)throw std::runtime_error("SDK project Lua limits not applied");}
        game->SetProjectAssetRoot(project.ResolvePath(std::filesystem::path(std::u8string(project.GetSettings().assetDirectory.begin(),project.GetSettings().assetDirectory.end()))));
        if(!game->ConfigureStartupScene(project.ResolvePath(project.GetSettings().startupScene),error)) throw std::runtime_error(error);
        game->RunSafely();return 0;
    } catch(const Engine::Base::RunFailure& error) {Engine::Base::ReportRunFailure(error);return 1;}
    catch(...) {Engine::Base::ReportRunException("native game error",std::current_exception());return 1;}
}
