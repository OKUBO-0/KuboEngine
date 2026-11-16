#include "engine/services/McpCommandBridge.h"
#include "engine/services/McpProtocolSession.h"
#include <iostream>
#include <stdexcept>

namespace { void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);} }
int main(){
    using namespace Engine;using Value=Scene::SceneValue;
    try {
        Scene::EntityWorld world;const auto id=world.CreateEntity("Render target"),peer=world.CreateEntity("Peer");
        const auto retired=world.CreateEntity("Retired");world.DestroyEntity(retired);
        world.Find(id)->script.started=true;world.Find(peer)->script.started=true;world.Find(peer)->rigidBody.grounded=true;
        Services::McpCommandBridge bridge({});
        const auto call=[&](const char* component,const char* patch){
            Value request;request["id"]=1.0;request["method"]="scene.set_component";
            request["params"]["entityId"]=double(id);request["params"]["component"]=component;
            request["params"]["patch"]=Value::parse(patch);return bridge.Handle(request,world);
        };
        Require(call("light",R"({"type":"spot","color":[1,0.5,0.25],"intensity":8,"range":30,"decay":3,"innerAngle":0.8,"outerAngle":1})").contains("result"),"Light addition failed");
        Require(world.Find(id)->hasLight && world.Find(id)->light.kind==Scene::LightKind::Spot && world.Find(id)->light.range==30,"Light fields lost");
        Require(call("light",R"({"outerAngle":0.9})").contains("result") && world.Find(id)->light.innerAngle==0.8f && world.Find(id)->light.intensity==8,"Partial Light patch was decoded against defaults");
        Require(!call("light",R"({"outerAngle":0.9})")["result"]["updated"].get<bool>(),"Light no-op claimed a change");
        const auto beforeCone=world.Capture().dump();
        Require(call("light",R"({"outerAngle":0.7,"enabled":false})").contains("error") && world.Capture().dump()==beforeCone,"Coupled cone rejection partially published");
        Require(call("material",R"({"lighting":true,"color":[0.25,0.5,0.75,0.8],"shininess":128,"roughness":0.4,"reflectionStrength":0.6,"textureInfluence":0.2})").contains("result"),"Material addition failed");
        Require(world.Find(id)->hasMaterial && world.Find(id)->material.lighting && world.Find(id)->material.shininess==128,"Material fields lost");
        Require(call("material",R"({"roughness":0.8})").contains("result") && world.Find(id)->material.reflectionStrength==0.6f && world.Find(id)->material.shininess==128,"Partial Material patch lost fields");
        Require(!call("material",R"({"roughness":0.8})")["result"]["updated"].get<bool>(),"Material no-op claimed a change");
        for(const auto component:{"light","material"}) {
            const auto snapshot=world.Capture().dump();
            for(const auto patch:{"{}","true","[]",R"({"unknown":true})",R"({"color":null})",R"({"color":[true,0,0]})",R"({"color":[1,0,0,0,0]})",R"({"color":[-0.1,0,0]})"})
                Require(call(component,patch).contains("error") && world.Capture().dump()==snapshot,"Invalid shared patch changed World");
        }
        const auto snapshot=world.Capture().dump();
        for(const auto patch:{R"({"enabled":1})",R"({"type":"Spot"})",R"({"type":1})",R"({"intensity":-1})",R"({"intensity":1000001})",R"({"range":0})",R"({"range":1e-100})",R"({"decay":17})",R"({"innerAngle":-0.1})",R"({"outerAngle":2})",R"({"color":[1,0]})",R"({"color":[1,0,1.01]})"})
            Require(call("light",patch).contains("error") && world.Capture().dump()==snapshot,"Invalid Light accepted");
        for(const auto patch:{R"({"enabled":true})",R"({"lighting":1})",R"({"shininess":0})",R"({"shininess":1025})",R"({"roughness":1.01})",R"({"reflectionStrength":-0.1})",R"({"textureInfluence":2})",R"({"color":[1,1,1]})",R"({"color":[1,1,1,1.01]})"})
            Require(call("material",patch).contains("error") && world.Capture().dump()==snapshot,"Invalid Material accepted");
        Require(world.Find(id)->script.started && world.Find(peer)->script.started && world.Find(peer)->rigidBody.grounded && world.CreateEntity("After edits")==retired+1,"Edits reset unrelated transient state or allocator");
        for(const auto component:{"light","material"}) {
            Require(call(component,"null")["result"]["updated"].get<bool>(),"Removal failed");
            Require(!call(component,"null")["result"]["updated"].get<bool>(),"Repeated removal was not a no-op");
        }
        Require(!world.Find(id)->hasLight && !world.Find(id)->hasMaterial && world.Find(id)->light.range==10 && world.Find(id)->material.shininess==60,"Removal did not restore defaults");
        for(const char* protocol:{"2025-11-25","2024-11-05"}) {
            int forwarded=0;Services::McpProtocolSession session([&](const Value& request){++forwarded;return bridge.Handle(request,world);});
            Value initialize=Value::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{},"clientInfo":{"name":"render-test","version":"1"}}})");
            initialize["params"]["protocolVersion"]=protocol;session.Handle(initialize);
            session.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
            const auto tools=session.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")["result"]["tools"];
            Require(tools.get<crude_json::array>().size()==14 && tools[6]["inputSchema"]["oneOf"].get<crude_json::array>().size()==10,"Render component schema missing or tool count changed");
            const auto send=[&](const char* component,const char* patch){
                Value request;request["jsonrpc"]="2.0";request["id"]=3.0;request["method"]="tools/call";
                request["params"]["name"]="scene_set_component";auto& arguments=request["params"]["arguments"];
                arguments["entityId"]=double(id);arguments["component"]=component;arguments["patch"]=Value::parse(patch);return session.Handle(request);
            };
            for(const auto component:{"light","material"}) {
                Require(send(component,R"({"color":[1,0]})")["result"]["isError"].get<bool>() && forwarded==0,"Invalid render transport reached bridge");
            }
            Require(!send("light",R"({"type":"point","intensity":2})")["result"]["isError"].get<bool>() && forwarded==1,"Light protocol forwarding failed");
            const auto success=send("material",R"({"lighting":true,"color":[1,0,0,1]})");
            Require(!success["result"]["isError"].get<bool>() && forwarded==2 && (success["result"].contains("structuredContent")== (std::string(protocol)=="2025-11-25")),"Material protocol response failed");
            Require(!send("light","null")["result"]["isError"].get<bool>() && !send("material","null")["result"]["isError"].get<bool>(),"Protocol removal failed");
        }
        std::cout<<"PASS: MCP Light/Material partial patch/defaults/no-op/removal, strict fields/ranges/cones/colors, both protocols, transport rejection and unrelated transient/allocator preservation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
