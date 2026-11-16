#include "engine/services/McpCommandBridge.h"
#include "engine/services/McpProtocolSession.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(){
    using namespace Engine;using Value=Scene::SceneValue;
    static_assert(std::is_nothrow_copy_assignable_v<Scene::CameraComponent>);
    static_assert(std::is_nothrow_copy_assignable_v<Physics::ColliderComponent>);
    static_assert(std::is_nothrow_copy_assignable_v<Physics::RigidBodyComponent>);
    try {
        Scene::EntityWorld world;const auto id=world.CreateEntity("Target"),peer=world.CreateEntity("Peer");
        const auto retired=world.CreateEntity("Retired");world.DestroyEntity(retired);
        world.Find(id)->script.started=true;world.Find(peer)->script.started=true;world.Find(peer)->rigidBody.grounded=true;
        Services::McpCommandBridge bridge({});
        const auto call=[&](const char* component,const char* patch){
            Value request;request["id"]=1.0;request["method"]="scene.set_component";
            request["params"]["entityId"]=double(id);request["params"]["component"]=component;
            request["params"]["patch"]=Value::parse(patch);return bridge.Handle(request,world);
        };
        Require(call("camera",R"({"enabled":true,"primary":true,"projection":"orthographic","orthographicHeight":12})").contains("result") && world.Find(id)->hasCamera && world.Find(id)->camera.orthographic && world.Find(id)->camera.orthographicHeight==12,"Camera addition failed");
        const auto cameraBefore=world.Capture().dump();
        Require(call("camera",R"({"nearClip":10000,"primary":false})").contains("error") && world.Capture().dump()==cameraBefore,"Invalid Camera cross-field update was partially published");
        Require(call("camera",R"({"orthographicHeight":12})")["result"]["updated"].get<bool>()==false,"No-op component claimed a change");
        Require(call("collider",R"({"enabled":true,"halfExtents":[1,2,3],"friction":0.75,"restitution":0.25})").contains("result") && world.Find(id)->hasCollider && world.Find(id)->collider.halfExtents.y==2,"Collider addition failed");
        Require(call("collider",R"({"trigger":true})").contains("result") && world.Find(id)->collider.halfExtents.z==3 && world.Find(id)->collider.friction==0.75f,"Partial Collider patch lost omitted fields");
        Require(call("rigidBody",R"({"enabled":true,"mass":2,"velocity":[1,2,3],"dynamic":false,"gravity":false})").contains("result") && world.Find(id)->hasRigidBody && world.Find(id)->rigidBody.mass==2 && !world.Find(id)->rigidBody.useGravity,"Body addition failed");
        for(const auto component:{"camera","collider","rigidBody"}) {
            const auto snapshot=world.Capture().dump();
            for(const auto patch:{"{}","true","[]",R"({"enabled":1})",R"({"unknown":true})"})
                Require(call(component,patch).contains("error") && world.Capture().dump()==snapshot,"Invalid patch mutated component");
        }
        for(const auto patch:{R"({"halfExtents":[-1,2,3]})",R"({"halfExtents":[1,2]})",R"({"halfExtents":[true,2,3]})",R"({"friction":1.1})",R"({"restitution":-0.1})"})Require(call("collider",patch).contains("error"),"Invalid Collider field accepted");
        for(const auto patch:{R"({"mass":0})",R"({"mass":1e-100})",R"({"mass":1e100})",R"({"velocity":[1e100,0,0]})",R"({"dynamic":"false"})"})Require(call("rigidBody",patch).contains("error"),"Invalid Body field accepted");
        Require(call("camera",R"({"projection":"invalid"})").contains("error") && call("camera",R"({"fovY":10})").contains("error"),"Invalid Camera settings accepted");
        Require(world.Find(id)->script.started && world.Find(peer)->script.started && world.Find(peer)->rigidBody.grounded && world.CreateEntity("After patches")==retired+1,"Component edits reset unrelated transient state or ID high-water");
        for(const auto component:{"camera","collider","rigidBody"})Require(call(component,"null").contains("result"),"Component removal failed");
        Require(!world.Find(id)->hasCamera && !world.Find(id)->hasCollider && !world.Find(id)->hasRigidBody && world.Find(id)->collider.halfExtents.x==0.5f,"Removal did not reset authored defaults");
        Require(!call("rigidBody","null")["result"]["updated"].get<bool>(),"Repeated removal was not a no-op");
        for(const char* protocol:{"2025-11-25","2024-11-05"}) {
            int forwarded=0;Services::McpProtocolSession session([&](const Value& request){++forwarded;return bridge.Handle(request,world);});
            Value initialize=Value::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");initialize["params"]["protocolVersion"]=protocol;session.Handle(initialize);
            session.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
            const auto tools=session.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")["result"]["tools"];
            Require(tools.get<crude_json::array>().size()==8 && tools[6]["inputSchema"]["oneOf"].get<crude_json::array>().size()==10 && tools[6]["annotations"]["destructiveHint"].get<bool>(),"Component schema missing");
            for(const char* arguments:{"{}",R"({"entityId":1,"component":"prefab","patch":null})",R"({"entityId":1,"component":"camera"})",R"({"entityId":1,"component":"camera","patch":{"mass":1}})",R"({"entityId":0,"component":"camera","patch":null})",R"({"entityId":1.5,"component":"camera","patch":null})",R"({"entityId":9007199254740992,"component":"camera","patch":null})",R"({"entityId":1,"component":"camera","patch":null,"extra":true})"}) {
                Value request;request["jsonrpc"]="2.0";request["id"]=3.0;request["method"]="tools/call";request["params"]["name"]="scene_set_component";request["params"]["arguments"]=Value::parse(arguments);
                Require(session.Handle(request)["result"]["isError"].get<bool>() && forwarded==0,"Invalid component request forwarded");
            }
            Require(session.HandleText(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"scene_set_component","arguments":{"entityId":1,"component":"camera","patch":{"enabled":true}}}})").is_null() && forwarded==0,"Notification mutated component");
            const auto success=session.HandleText(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"scene_set_component","arguments":{"entityId":1,"component":"collider","patch":{"enabled":true}}}})");
            Require(!success["result"]["isError"].get<bool>() && forwarded==1 && (success["result"].contains("structuredContent")== (std::string(protocol)=="2025-11-25")),"Component forwarding/version response failed");
            const auto missing=session.HandleText(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scene_set_component","arguments":{"entityId":999,"component":"camera","patch":null}}})");
            Require(missing.contains("result") && missing["result"]["isError"].is_boolean() && missing["result"]["isError"].get<bool>(),"Missing Entity edit accepted");
        }
        std::cout<<"PASS: MCP Camera/Collider/RigidBody patch/remove schemas, both protocols, strict fields/ranges, candidate isolation, no-op and transient/allocator preservation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
