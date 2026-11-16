#include "../engine/services/EditorMcpSession.h"
#include "../engine/editor/EntityDocumentState.h"
#include "../engine/assets/AssetRegistry.h"
#include <future>
#include <iostream>

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using Engine::Services::EditorMcpSession;
    using Value = Engine::Scene::SceneValue;
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-editor-mcp-日本語-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        std::filesystem::create_directories(fixture);
        Engine::Scene::EntityWorld world;
        const auto authored = world.CreateEntity("Authored in Editor");
        Engine::Editor::EntityHistory history;
        Engine::Editor::EntityHistory* activeHistory = &history;
        Engine::Scene::EntityId selection = authored;
        Engine::Editor::EntitySelection fullSelection;bool useFullSelection=false;
        Engine::Editor::EntityDocumentState document;
        Engine::Assets::AssetRegistry assetRegistry;bool useAssetRegistry=false;
        std::string error;
        int saveNotifications = 0;
        const auto currentScene = fixture / "Assets/Scenes/current.scene";
        Require(world.Save(currentScene, error), "Could not create current document fixture");
        document.Opened(world, currentScene);
        EditorMcpSession session;
        Require(session.Start(fixture, error), error.c_str());
        EditorMcpSession duplicate;
        Require(!duplicate.Start(fixture, error), "Second Editor took over the active session");
        Require(std::filesystem::is_regular_file(EditorMcpSession::Descriptor(fixture)), "Failed duplicate start deleted active descriptor");
        // Reject stale/malformed ownership before any Scene request is sent.
        const auto descriptorCopyRoot = fixture / "DescriptorCopies";
        std::filesystem::create_directories(EditorMcpSession::Descriptor(descriptorCopyRoot).parent_path());
        std::ifstream published(EditorMcpSession::Descriptor(fixture), std::ios::binary);
        const auto descriptor = Value::parse(std::string(std::istreambuf_iterator<char>(published), {}));
        published.close();
        const auto ownerWorldBefore = world.Capture().dump();
        const auto refuseOwner = [&](Value invalid, const char* diagnostic) {
            { std::ofstream copy(EditorMcpSession::Descriptor(descriptorCopyRoot), std::ios::binary); copy << invalid.dump(); Require(bool(copy), "Descriptor copy fixture write failed"); }
            bool refused = false;
            try { EditorMcpSession::Call(descriptorCopyRoot, Value::parse(R"({"id":99,"method":"scene.create_entity","params":{"name":"Must not publish"}})")); }
            catch (const std::exception& failure) { refused = std::string(failure.what()).find(diagnostic) != std::string::npos; }
            Require(refused && session.IsRunning() && world.Capture().dump() == ownerWorldBefore, "Invalid owner changed live Scene or lost expected diagnostic");
        };
        const auto refuseRaw = [&](const std::string& raw) {
            const auto copiedPath = EditorMcpSession::Descriptor(descriptorCopyRoot);
            { std::ofstream copied(copiedPath, std::ios::binary); copied << raw; Require(bool(copied), "Raw fixture write failed"); }
            bool refused = false;
            try { EditorMcpSession::Call(descriptorCopyRoot, Value::parse(R"({"id":99,"method":"scene.create_entity","params":{"name":"Must not publish"}})")); }
            catch (const std::exception& failure) { refused = std::string(failure.what()) == "Invalid Editor MCP descriptor"; }
            std::ifstream retained(copiedPath, std::ios::binary);
            const std::string after{std::istreambuf_iterator<char>(retained), {}};
            Require(refused && after == raw && session.IsRunning() && world.Capture().dump() == ownerWorldBefore,
                "Ambiguous/deep metadata changed evidence or live World");
        };
        auto raw = descriptor.dump(); raw.pop_back();
        refuseRaw(raw + ",\"pid\":0}");
        refuseRaw(raw + ",\"p\\u0069d\":0}");
        refuseRaw(raw + ",\"process_creation_low\":0}");
        refuseRaw(raw + ",\"unknown\":[[[[[[[[0]]]]]]]]}");
        std::cout << "PASS: Call rejects duplicate literal/escaped identity and depth before IPC; exact evidence/live World retained\n";
        auto invalid = descriptor; invalid["pid"] = 0.0; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["pid"] = -1.0; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["pid"] = 1.5; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["pid"] = 4294967296.0; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["process_creation_high"] = true; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["process_creation_low"] = nullptr; refuseOwner(invalid, "Invalid Editor MCP process identity");
        invalid = descriptor; invalid["process_creation_low"] = double(static_cast<DWORD>(descriptor["process_creation_low"].get<crude_json::number>()) ^ 1);
        refuseOwner(invalid, "Editor MCP owner creation time changed");
        invalid = descriptor; invalid["format"] = "kubo-editor-mcp-1"; refuseOwner(invalid, "Legacy Editor MCP descriptor");
        std::cout << "PASS: published exact creation identity and eight invalid/stale/legacy metadata refusals before Scene mutation\n";
        const auto call = [&](const char* json, bool editing = true) {
            const auto request = Value::parse(json);
            auto result = std::async(std::launch::async, [&]() { return EditorMcpSession::Call(fixture, request); });
            for (int frame = 0; frame < 1000 && result.wait_for(std::chrono::seconds(0)) != std::future_status::ready; ++frame) {
                session.Tick(world, editing && !document.HasPendingAction(), activeHistory, selection,
                    [&](const std::filesystem::path& target, const Engine::Scene::EntityWorld& savedWorld) {
                        ++saveNotifications;
                        document.MarkSavedAt(savedWorld, target);
                    },useFullSelection?&fullSelection:nullptr,useAssetRegistry?&assetRegistry:nullptr,
                    [&]() {
                        Value status = crude_json::type_t::object;
                        status["canEditScene"] = editing && !document.HasPendingAction();
                        return status;
                    });
                if(useFullSelection)selection=fullSelection.Primary();
                Sleep(1);
            }
            Require(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready, "MCP IPC response was not ready");
            if (editing && !document.HasPendingAction()) document.Observe(world);
            return result.get();
        };
        const auto listed = call(R"({"id":1,"method":"scene.list_entities"})");
        Require(listed.contains("result") && listed["result"].get<crude_json::array>().size() == 1,
            "IPC did not read the live Editor world");
        const auto lockedStatus = call(R"({"id":100,"method":"editor.get_status","params":{}})", false);
        Require(lockedStatus.contains("result") && !lockedStatus["result"]["canEditScene"].get<bool>(),
            "IPC could not read the Editor status while Scene edits were locked");
        Require(call(R"({"id":101,"method":"editor.get_status","params":{"unexpected":true}})", false)
            ["error"]["code"].get<double>() == -32602, "IPC accepted unexpected status arguments");
        Require(world.Capture().dump() == ownerWorldBefore && history.UndoCount() == 0,
            "Status calls modified the Scene or history");
        const auto created = call(R"({"id":2,"method":"scene.create_entity","params":{"name":"Remote entity"}})");
        Require(created.contains("result") && world.GetEntities().size() == 2 && world.Find(authored),
            "IPC creation did not change the live Editor world");
        Require(document.IsDirty(), "Accepted MCP edit did not dirty the authored document");
        Require(history.UndoCount() == 1, "Accepted IPC edit was not recorded");
        Require(history.Undo(world, selection, error) && world.GetEntities().size() == 1, "IPC edit undo failed");
        Require(history.Redo(world, selection, error) && world.GetEntities().size() == 2, "IPC edit redo failed");
        Engine::Editor::EntityHistory tooSmall(100, 1);
        activeHistory = &tooSmall;
        const auto retired = world.CreateEntity("Retired ID");
        world.DestroyEntity(retired);
        const auto beforeRejected = world.Capture().dump();
        const auto rejected = call(R"({"id":20,"method":"scene.create_entity","params":{"name":"Cannot record"}})");
        Require(rejected.contains("error") && rejected["error"]["code"].get<double>() == -32000 &&
            world.Capture().dump() == beforeRejected && !tooSmall.CanUndo(), "Unrecordable MCP edit was acknowledged or not rolled back");
        const auto probe = world.CreateEntity("Allocator probe");
        Require(probe == retired + 1, "Rejected MCP mutation changed the ID allocator");
        world.DestroyEntity(probe);
        activeHistory = &history;
        const auto paused = call(R"({"id":3,"method":"scene.create_entity"})", false);
        Require(paused.contains("error") && world.GetEntities().size() == 2, "Playback allowed scene mutations");
        Require(history.UndoCount() == 1, "Rejected playback mutation changed history");
        Require(call(R"({"id":4,"method":"scene.list_entities"})", false).contains("result"), "Playback blocked read-only listing");
        const auto beforeUi = world.Capture();
        world.Find(authored)->name = "Pending UI name edit";
        Require(history.Record(beforeUi, world, selection, selection, error), "Pending UI edit could not be recorded");
        Require(call(R"({"id":21,"method":"scene.create_entity","params":{"name":"Separate remote command"}})").contains("result"), "Second remote edit failed");
        Require(history.UndoCount() == 3, "Remote edit was merged with pending UI drag");
        Require(history.Undo(world, selection, error) && world.GetEntities().size() == 2 &&
            world.Find(authored)->name == "Pending UI name edit", "Remote undo also rewound the UI edit");
        Require(history.Undo(world, selection, error) && world.Find(authored)->name == "Authored in Editor", "Pending UI command undo failed");
        Require(history.Redo(world, selection, error) && world.Find(authored)->name == "Pending UI name edit", "UI command redo failed");
        Require(call(R"({"id":5,"method":"scene.save","params":{"path":"Assets/Scenes/remote.scene"}})").contains("result"), "IPC save failed");
        Require(document.IsDirty() && saveNotifications == 1, "Alternate scene save cleaned the current document");
        Engine::Scene::EntityWorld saved;
        Require(saved.Load(fixture / "Assets/Scenes/remote.scene", error) && saved.GetEntities().size() == 2, "Saved IPC scene differs from live world");
        Require(call(R"({"id":6,"method":"scene.save","params":{"path":"C:outside.json"}})").contains("error"), "Drive-relative path was allowed");
        Require(saveNotifications == 1, "Failed save issued a saved-document notification");
        Require(call(R"({"id":22,"method":"scene.save","params":{"path":"Assets/Scenes/current.scene"}})").contains("result") &&
            !document.IsDirty() && saveNotifications == 2, "Current scene save did not update its baseline");
        Require(call(R"({"id":23,"method":"scene.create_entity","params":{"name":"Unsaved before close"}})").contains("result"), "Dirty-close fixture mutation failed");
        Require(document.Request(Engine::Editor::EntityDocumentState::Action::Close) && document.NeedsConfirmation(), "Dirty close did not require confirmation");
        Require(call(R"({"id":24,"method":"scene.create_entity"})").contains("error") && world.GetEntities().size() == 3,
            "Pending document action allowed a mutation");
        Require(call(R"({"id":25,"method":"scene.save","params":{"path":"Assets/Scenes/current.scene"}})").contains("error") &&
            document.IsDirty() && saveNotifications == 2, "Pending document action allowed an MCP save");
        Require(call(R"({"id":26,"method":"scene.list_entities"})").contains("result"), "Pending document action blocked read-only listing");
        document.Cancel();
        Require(history.Undo(world, selection, error), "Could not undo dirty-close fixture mutation");
        document.Observe(world);
        Require(!document.IsDirty(), "Undo to the saved MCP baseline remained dirty");
        history.Clear();
        const auto child=world.CreateEntity("Child of deletion target",authored);
        const auto parentRequest=[&](Engine::Scene::EntityId entity,Engine::Scene::EntityId parent,bool editing=true,bool keepWorld=false) {
            const auto text="{\"id\":50,\"method\":\"scene.set_parent\",\"params\":{\"entityId\":"+std::to_string(entity)+",\"parentId\":"+std::to_string(parent)+",\"keepWorld\":"+(keepWorld?"true":"false")+"}}";
            return call(text.c_str(),editing);
        };
        const auto parentBefore=world.Capture().dump();
        document.Opened(world,currentScene);
        Require(parentRequest(child,0).contains("result") && world.Find(child)->parent==0 && history.UndoCount()==1 && document.IsDirty(),"Rooting child did not record history/dirty");
        Require(parentRequest(child,0)["result"]["updated"].get<bool>()==false && history.UndoCount()==1,"No-op parent added history");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==parentBefore,"Parent Undo failed");
        document.Observe(world);Require(!document.IsDirty(),"Parent Undo to baseline remained dirty");
        Require(history.Redo(world,selection,error) && world.Find(child)->parent==0,"Parent Redo failed");
        Require(history.Undo(world,selection,error),"Parent restore failed");history.Clear();
        Require(parentRequest(authored,child).contains("error") && parentRequest(child,child).contains("error") && parentRequest(child,999999).contains("error") && world.Capture().dump()==parentBefore,"Invalid parent/cycle changed hierarchy");
        Require(call(R"({"id":51,"method":"scene.set_parent","params":{"entityId":1,"parentId":0.5}})").contains("error") && world.Capture().dump()==parentBefore,"IPC bypass accepted malformed parent ID");
        Require(parentRequest(child,0,false).contains("error") && world.Capture().dump()==parentBefore,"Playback allowed reparenting");
        const auto savedParent=world.Find(authored)->transform,savedChild=world.Find(child)->transform;
        world.Find(authored)->transform.position.x=10;world.Find(child)->transform.position.x=2;
        const auto keepBaseline=world.Capture().dump();document.Opened(world,currentScene);
        Require(parentRequest(child,0,true,true).contains("result") && world.Find(child)->transform.position.x==12 && history.UndoCount()==1 && document.IsDirty(),"Keep-World IPC did not map parent/local position into one dirty command");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==keepBaseline,"Keep-World Undo failed");
        Require(history.Redo(world,selection,error) && world.Find(child)->parent==0 && world.Find(child)->transform.position.x==12,"Keep-World Redo failed");
        Require(history.Undo(world,selection,error),"Keep-World restore failed");history.Clear();
        activeHistory=&tooSmall;
        Require(parentRequest(child,0,true,true).contains("error") && world.Capture().dump()==keepBaseline,"Keep-World history rejection did not retain local Transform/parent");activeHistory=&history;
        world.Find(authored)->transform=savedParent;world.Find(child)->transform=savedChild;document.Opened(world,currentScene);
        activeHistory=&tooSmall;
        Require(parentRequest(child,0).contains("error") && world.Capture().dump()==parentBefore,"Parent history refusal did not restore hierarchy");activeHistory=&history;
        world.Find(child)->parent=child;
        Require(parentRequest(authored,child).contains("error"),"Corrupt ancestry accepted/hung");world.Find(child)->parent=authored;
        world.Find(child)->transform.position={7,8,9};
        document.Opened(world,currentScene);
        const auto beforeTransform=world.Capture().dump();
        const auto transform=call(R"({"id":30,"method":"scene.set_transform","params":{"entityId":1,"position":[2,3,4],"rotation":[0,0.5,0]}})");
        Require(transform.contains("result") && world.Find(authored)->transform.position.x==2 && document.IsDirty() && history.UndoCount()==1,"IPC Transform did not record/dirty world");
        Require(call(R"({"id":31,"method":"scene.set_transform","params":{"entityId":1,"position":[2,3,4]}})").contains("result") && history.UndoCount()==1,"No-op Transform added history");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==beforeTransform,"IPC Transform Undo failed");
        Require(history.Redo(world,selection,error) && world.Find(authored)->transform.rotation.y==0.5f,"IPC Transform Redo failed");
        const auto afterTransform=world.Capture().dump();
        Require(call(R"({"id":32,"method":"scene.set_transform","params":{"entityId":1,"position":[99,99,99],"scale":[1e100,1,1]}})").contains("error") && world.Capture().dump()==afterTransform && history.UndoCount()==1,"Invalid Transform changed world/history");
        Require(call(R"({"id":33,"method":"scene.delete_entity","params":{"entityId":1}})",false).contains("error") && call(R"({"id":34,"method":"scene.set_transform","params":{"entityId":1,"position":[9,9,9]}})",false).contains("error") && world.Capture().dump()==afterTransform,"Playback allowed Transform/delete");
        activeHistory=&tooSmall;
        Require(call(R"({"id":35,"method":"scene.delete_entity","params":{"entityId":1}})").contains("error") && world.Capture().dump()==afterTransform,"History rejection did not roll back deletion/parent changes");
        activeHistory=&history;
        Require(call(R"({"id":36,"method":"scene.delete_entity","params":{"entityId":1}})").contains("result") && !world.Find(authored) && world.Find(child)->parent==0 && world.Find(child)->transform.position.x==7 && history.UndoCount()==2,"IPC delete semantics/history failed");
        Require(history.Undo(world,selection,error) && world.Find(authored) && world.Find(child)->parent==authored && selection==authored,"IPC delete Undo did not restore hierarchy/selection");
        Require(history.Redo(world,selection,error) && !world.Find(authored) && world.Find(child)->parent==0 && selection==0,"IPC delete Redo left stale selection");
        Require(document.Request(Engine::Editor::EntityDocumentState::Action::Close),"Pending document test failed");
        Require(call(R"({"id":37,"method":"scene.delete_entity","params":{"entityId":2}})").contains("error"),"Pending action allowed deletion");
        Require(parentRequest(child,0).contains("error"),"Pending document action allowed parent tool");
        document.Cancel();
        history.Clear();world=Engine::Scene::EntityWorld{};
        const auto first=world.CreateEntity("First"),second=world.CreateEntity("Second"),third=world.CreateEntity("Third");
        Require(fullSelection.Click(world,first,false,false,error) && fullSelection.Click(world,third,false,true,error),error.c_str());
        selection=fullSelection.Primary();useFullSelection=true;const auto fullBefore=fullSelection.Capture();
        Require(call(R"({"id":38,"method":"scene.set_transform","params":{"entityId":2,"position":[1,2,3]}})").contains("result") && fullSelection.Capture()==fullBefore,"IPC Transform collapsed multi-selection");
        fullSelection.Replace(first);
        Require(history.Undo(world,fullSelection,error) && fullSelection.Capture()==fullBefore,"IPC Undo lost full selection/anchor");selection=fullSelection.Primary();
        Require(history.Redo(world,fullSelection,error) && fullSelection.Capture()==fullBefore,"IPC Redo lost full selection");
        Require(call(R"({"id":39,"method":"scene.delete_entity","params":{"entityId":3}})").contains("result") && fullSelection.Ids()==std::vector<Engine::Scene::EntityId>{first,second} && fullSelection.Primary()==second,"IPC primary deletion lost surviving selection");
        const auto surviving=fullSelection.Capture();
        Require(history.Undo(world,fullSelection,error) && fullSelection.Capture()==fullBefore && world.Find(third),"IPC deletion Undo lost full selection");
        Require(history.Redo(world,fullSelection,error) && fullSelection.Capture()==surviving && !world.Find(third),"IPC deletion Redo lost surviving selection");
        selection=fullSelection.Primary();activeHistory=&tooSmall;const auto refused=world.Capture().dump();
        Require(call(R"({"id":40,"method":"scene.delete_entity","params":{"entityId":2}})").contains("error") && world.Capture().dump()==refused && fullSelection.Capture()==surviving,"IPC history refusal changed multi-selection/world");activeHistory=&history;
        history.Clear();document.Opened(world,currentScene);const auto componentBefore=world.Capture().dump();
        const auto componentRequest=R"({"id":50,"method":"scene.set_component","params":{"entityId":2,"component":"collider","patch":{"enabled":true,"halfExtents":[1,2,3],"friction":0.5}}})";
        Require(call(componentRequest).contains("result") && world.Find(second)->hasCollider && world.Find(second)->collider.friction==0.5f && history.UndoCount()==1 && document.IsDirty() && fullSelection.Capture()==surviving,"Component IPC failed history/dirty/full-selection");
        Require(call(componentRequest).contains("result") && history.UndoCount()==1,"No-op component IPC added history");
        Require(history.Undo(world,fullSelection,error) && world.Capture().dump()==componentBefore && fullSelection.Capture()==surviving,"Component Undo failed");selection=fullSelection.Primary();document.Observe(world);
        Require(!document.IsDirty(),"Component Undo lost saved baseline");
        Require(history.Redo(world,fullSelection,error) && world.Find(second)->hasCollider && fullSelection.Capture()==surviving,"Component Redo failed");selection=fullSelection.Primary();document.Observe(world);
        const auto componentAfter=world.Capture().dump();
        Require(call(R"({"id":51,"method":"scene.set_component","params":{"entityId":2,"component":"collider","patch":null}})",false).contains("error") && world.Capture().dump()==componentAfter,"Playback allowed component removal");
        Require(document.Request(Engine::Editor::EntityDocumentState::Action::Close),"Component pending action fixture failed");
        Require(call(componentRequest).contains("error") && world.Capture().dump()==componentAfter,"Pending document action allowed component edit");document.Cancel();
        activeHistory=&tooSmall;world.Find(second)->script.started=true;const auto componentSelection=fullSelection.Capture();
        Require(call(R"({"id":52,"method":"scene.set_component","params":{"entityId":2,"component":"collider","patch":null}})").contains("error") && world.Capture().dump()==componentAfter && world.Find(second)->script.started && fullSelection.Capture()==componentSelection,"Component history refusal lost World/transient/selection");activeHistory=&history;
        Require(call(R"({"id":53,"method":"scene.set_component","params":{"entityId":2,"component":"collider","patch":null}})").contains("result") && !world.Find(second)->hasCollider && history.UndoCount()==2,"Component removal IPC failed");
        Require(history.Undo(world,fullSelection,error) && world.Capture().dump()==componentAfter,"Component removal Undo failed");
        Require(history.Redo(world,fullSelection,error) && !world.Find(second)->hasCollider,"Component removal Redo failed");
        for(const char* file:{"automation.obj","automation.lua","automation.wav"}) {
            std::ofstream source(fixture/"Assets"/file,std::ios::binary);source<<"owned type/reference-only IPC fixture";
        }
        Require(assetRegistry.Scan(fixture/"Assets",error),error.c_str());useAssetRegistry=true;
        const auto assetCall=[&](const char* component,const Value& patch,bool editing=true){
            Value request;request["id"]=54.0;request["method"]="scene.set_component";request["params"]["entityId"]=double(second);
            request["params"]["component"]=component;request["params"]["patch"]=patch;return call(request.dump().c_str(),editing);
        };
        for(const char* component:{"model","script","audio"}) {
            history.Clear();document.Opened(world,currentScene);const auto beforeResource=world.Capture().dump();const auto high=world.AllocationHighWatermark();
            const auto filename=std::string("automation.")+(std::string(component)=="model"?"obj":std::string(component)=="script"?"lua":"wav");
            Value patch;patch["asset"]="Assets/"+filename;patch["guid"]=assetRegistry.FindByPath(fixture/"Assets"/filename)->guid;
            if(std::string(component)=="script"){patch["enabled"]=true;patch["properties"]["speed"]=2.5;patch["properties"]["label"]="日本語";}
            if(std::string(component)=="audio"){patch["volume"]=0.25;patch["loop"]=true;}
            Require(assetCall(component,patch).contains("result") && history.UndoCount()==1 && document.IsDirty() && fullSelection.Capture()==surviving && world.AllocationHighWatermark()==high,"Resource IPC binding lost history/dirty/selection/allocator");
            world.Find(second)->script.started=true;const auto bound=world.Capture().dump();
            Require(assetCall(component,patch).contains("result") && history.UndoCount()==1 && world.Find(second)->script.started,"Resource IPC no-op added history/reset lifecycle");
            Require(history.Undo(world,fullSelection,error) && world.Capture().dump()==beforeResource && fullSelection.Capture()==surviving,"Resource IPC Undo failed");selection=fullSelection.Primary();document.Observe(world);Require(!document.IsDirty(),"Resource Undo lost saved baseline");
            Require(history.Redo(world,fullSelection,error) && world.Capture().dump()==bound && fullSelection.Capture()==surviving,"Resource IPC Redo failed");selection=fullSelection.Primary();document.Observe(world);
            world.Find(second)->script.started=true;world.Find(second)->rigidBody.grounded=true;
            activeHistory=&tooSmall;
            Require(assetCall(component,Value{}).contains("error") && world.Capture().dump()==bound && world.Find(second)->script.started && world.Find(second)->rigidBody.grounded && fullSelection.Capture()==surviving && world.AllocationHighWatermark()==high,"Resource history refusal changed live/transient/selection/allocator");activeHistory=&history;
            Require(assetCall(component,Value{},false).contains("error") && world.Capture().dump()==bound,"Playback allowed resource removal");
            Require(document.Request(Engine::Editor::EntityDocumentState::Action::Close),"Resource pending fixture failed");
            Require(assetCall(component,Value{}).contains("error") && world.Capture().dump()==bound,"Pending document allowed resource removal");document.Cancel();
            Require(assetCall(component,Value{}).contains("result") && history.UndoCount()==2,"Resource removal did not enter history");
            Require(history.Undo(world,fullSelection,error) && world.Capture().dump()==bound,"Resource removal Undo failed");selection=fullSelection.Primary();
            Require(history.Redo(world,fullSelection,error) && fullSelection.Capture()==surviving,"Resource removal Redo failed");selection=fullSelection.Primary();
        }
        history.Clear();world.Clear();world.CreateEntity("Last identity");auto exhausted=world.Capture();
        const auto lastId=Engine::Scene::EntityWorld::MaximumEntityId;exhausted["entities"].get<crude_json::array>()[0]["id"]=double(lastId);
        Require(world.Restore(exhausted,error),error.c_str());fullSelection.Replace(lastId);selection=lastId;useFullSelection=true;
        document.Opened(world,currentScene);const auto exhaustedBefore=world.Capture().dump();const auto exhaustedSelection=fullSelection.Capture();
        Require(call(R"({"id":41,"method":"scene.create_entity","params":{"name":"No identity"}})").contains("error") && world.Capture().dump()==exhaustedBefore &&
            fullSelection.Capture()==exhaustedSelection && selection==lastId && !history.CanUndo() && !document.IsDirty(),"IPC allocation exhaustion changed world/selection/history/dirty");
        session.Stop();
        Require(!std::filesystem::exists(EditorMcpSession::Descriptor(fixture)), "Disable did not remove session descriptor");
        bool disconnected = false;
        try { EditorMcpSession::Call(fixture, Value::parse(R"({"id":7,"method":"scene.list_entities"})")); }
        catch (...) { disconnected = true; }
        Require(disconnected, "Disabled session still accepted calls");
        Require(session.Start(fixture, error), "Reenable after Stop failed");
        session.Stop();
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: live Editor MCP IPC, single session, create/list/save, playback mutation guard and enable/disable lifecycle\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(fixture);
        return 1;
    }
}
