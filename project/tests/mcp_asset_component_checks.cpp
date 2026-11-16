#include "engine/services/McpCommandBridge.h"
#include "engine/services/McpProtocolSession.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/base/AssetPath.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <map>
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;file.close();Require(!file.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int main(){
    using namespace Engine;using Value=Scene::SceneValue;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-mcp-assets-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        const auto assetRoot=fixture/L"Assets/資料";std::filesystem::create_directories(assetRoot);
        const auto model=assetRoot/L"モデル.obj",script=assetRoot/L"動作.lua",audio=assetRoot/L"音声.wav";
        Write(model,"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");Write(script,"error('MCP binding must not execute this source')");Write(audio,"File existence/type fixture, not an audio decoder test");
        Assets::AssetRegistry registry;std::string error;Require(registry.Scan(assetRoot,error),error.c_str());
        const auto modelGuid=registry.FindByPath(model)->guid,scriptGuid=registry.FindByPath(script)->guid,audioGuid=registry.FindByPath(audio)->guid;
        std::map<std::filesystem::path,std::string> unchanged;
        for(const auto& file:std::filesystem::recursive_directory_iterator(assetRoot))if(file.is_regular_file())unchanged[file.path()]=Read(file.path());
        Scene::EntityWorld world;const auto target=world.CreateEntity("Target"),peer=world.CreateEntity("Peer"),retired=world.CreateEntity("Retired");world.DestroyEntity(retired);
        world.Find(target)->rigidBody.grounded=true;world.Find(peer)->script.started=true;
        Services::McpCommandBridge bridge(fixture,&registry);
        const auto call=[&](const char* component,const Value& patch){Value request;request["id"]=1.0;request["method"]="scene.set_component";request["params"]["entityId"]=double(target);request["params"]["component"]=component;request["params"]["patch"]=patch;return bridge.Handle(request,world);};
        Value modelPatch;modelPatch["asset"]="Assets/資料/old.obj";modelPatch["guid"]=modelGuid;
        Require(call("model",modelPatch).contains("result") && world.Find(target)->hasModel && world.Find(target)->model.assetGuid==modelGuid,"Authoritative GUID Model binding with absent hint failed");
        Require(!call("model",modelPatch)["result"]["updated"].get<bool>(),"Model no-op claimed change");
        Value scriptPatch;scriptPatch["asset"]="Assets/資料/old.lua";scriptPatch["guid"]=scriptGuid;scriptPatch["enabled"]=true;
        scriptPatch["properties"]["speed"]=2.5;scriptPatch["properties"]["flag"]=true;scriptPatch["properties"]["label"]="日本語";
        Require(call("script",scriptPatch).contains("result") && world.Find(target)->hasScript && world.Find(target)->script.properties.size()==3,"Script GUID/properties binding failed or executed Lua");
        world.Find(target)->script.started=true;
        Require(!call("script",scriptPatch)["result"]["updated"].get<bool>() && world.Find(target)->script.started,"Script no-op reset lifecycle");
        Value properties;properties["properties"]=Value::parse(R"({"speed":3})");
        Require(call("script",properties).contains("result") && world.Find(target)->script.properties.size()==1 && std::get<double>(world.Find(target)->script.properties.at("speed"))==3 && !world.Find(target)->script.started,"Script override replacement did not reset only targeted lifecycle");
        properties["properties"]=crude_json::object{};Require(call("script",properties).contains("result") && world.Find(target)->script.properties.empty(),"Script override clearing failed");
        Value audioPatch;audioPatch["guid"]=audioGuid;audioPatch["asset"]="Assets/資料/old.wav";audioPatch["volume"]=0.25;audioPatch["loop"]=true;
        Require(call("audio",audioPatch).contains("result") && world.Find(target)->hasAudio && world.Find(target)->audio.loop,"Audio GUID binding failed");
        Value volume;volume["volume"]=0.5;Require(call("audio",volume).contains("result") && world.Find(target)->audio.assetGuid==audioGuid && world.Find(target)->audio.loop,"Audio partial patch lost reference/omitted fields");
        const auto baseline=world.Capture().dump();const auto high=world.AllocationHighWatermark();
        const auto refused=[&](const char* component,const Value& patch){Require(call(component,patch).contains("error") && world.Capture().dump()==baseline && world.AllocationHighWatermark()==high && world.Find(peer)->script.started && world.Find(target)->rigidBody.grounded,"Rejected asset patch changed World/transient/allocator");};
        for(const char* patch:{R"({"asset":"../escape.obj"})",R"({"asset":"C:escape.obj"})",R"({"asset":"/escape.obj"})",R"({"asset":"Assets/file.obj:stream"})",R"({"guid":"BAD"})",R"({"guid":""})",R"({"guid":true})",R"({"asset":"Assets/資料/missing.obj","guid":null})"})refused("model",Value::parse(patch));
        Value wrongType;wrongType["guid"]=scriptGuid;refused("model",wrongType);
        Value outside;outside["asset"]="Outside.obj";outside["guid"]=Value{};Write(fixture/L"Outside.obj","owned outside-asset-root fixture");refused("model",outside);
        Value invalidProperties;invalidProperties["properties"]["speed"]=Value{};refused("script",invalidProperties);
        invalidProperties["properties"]=crude_json::object{};invalidProperties["properties"]["big"]=1e100;refused("script",invalidProperties);
        invalidProperties["properties"]=crude_json::object{};invalidProperties["properties"]["text"]=std::string(1025,'x');refused("script",invalidProperties);
        invalidProperties["properties"]=crude_json::object{};for(int i=0;i<65;++i)invalidProperties["properties"][std::to_string(i)]=1.0;refused("script",invalidProperties);
        Value malformed;malformed["properties"][std::string(1,char(0xff))]=true;refused("script",malformed);
        Value metadata;metadata["version"]=1.0;metadata["guid"]="0000000000000000";Write(Assets::AssetRegistry::MetadataPath(model),metadata.dump());refused("model",modelPatch);Write(Assets::AssetRegistry::MetadataPath(model),unchanged.at(Assets::AssetRegistry::MetadataPath(model)));
        std::filesystem::remove(audio);refused("audio",volume);Write(audio,unchanged.at(audio));
        std::filesystem::create_directories(assetRoot/".kubo");Write(assetRoot/".kubo/asset-move.pending.json","{}");refused("script",properties);std::filesystem::remove(assetRoot/".kubo/asset-move.pending.json");
        Value pathBinding;pathBinding["asset"]=Base::PathToUtf8(model.lexically_relative(fixture));pathBinding["guid"]=Value{};
        Require(call("model",pathBinding).contains("result") && world.Find(target)->model.assetGuid.empty(),"Explicit GUID clearing/path binding failed");
        Services::McpCommandBridge noRegistry(fixture);Value missingContext;missingContext["method"]="scene.set_component";missingContext["params"]["entityId"]=double(target);missingContext["params"]["component"]="model";missingContext["params"]["patch"]=pathBinding;
        const auto beforeContext=world.Capture().dump();Require(noRegistry.Handle(missingContext,world).contains("error") && world.Capture().dump()==beforeContext,"Missing active registry accepted asset binding");
        for(const auto component:{"model","script","audio"})Require(call(component,Value{}).contains("result"),"Resource component removal failed");
        Require(!world.Find(target)->hasModel && !world.Find(target)->hasScript && !world.Find(target)->hasAudio && world.Find(peer)->script.started && world.Find(target)->rigidBody.grounded && world.CreateEntity("Next")==retired+1,"Resource removal lost unrelated state/IDs");
        Value clearAbsent;clearAbsent["guid"]=Value{};
        Require(!call("audio",clearAbsent)["result"]["updated"].get<bool>() && !world.Find(target)->hasAudio,"Clearing absent Audio GUID created a null component or changed World");
        Value unassigned;unassigned["volume"]=0.0;Require(call("audio",unassigned).contains("result") && world.Find(target)->hasAudio && world.Find(target)->audio.assetPath.empty(),"Unassigned Audio authoring failed");
        for(const char* protocol:{"2025-11-25","2024-11-05"}) {
            int forwarded=0;Services::McpProtocolSession session([&](const Value& request){++forwarded;return bridge.Handle(request,world);});
            Value initialize=Value::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");initialize["params"]["protocolVersion"]=protocol;session.Handle(initialize);
            session.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
            const auto tool=session.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")["result"]["tools"][6];
            Require(tool["inputSchema"]["oneOf"].get<crude_json::array>().size()==10 && tool["inputSchema"]["oneOf"][4]["properties"]["patch"]["anyOf"][0]["properties"]["properties"]["maxProperties"].get<double>()==64,"Resource Component schemas missing");
            Value request;request["jsonrpc"]="2.0";request["id"]=3.0;request["method"]="tools/call";request["params"]["name"]="scene_set_component";request["params"]["arguments"]["entityId"]=double(target);request["params"]["arguments"]["component"]="script";
            request["params"]["arguments"]["patch"]=invalidProperties;
            Require(session.Handle(request)["result"]["isError"].get<bool>() && forwarded==0,"Invalid resource scalar properties forwarded");
            request["params"]["arguments"]["patch"]=scriptPatch;
            const auto success=session.Handle(request);
            Require(!success["result"]["isError"].get<bool>() && forwarded==1 && (success["result"].contains("structuredContent")==(std::string(protocol)=="2025-11-25")),"Resource forwarding/version response failed");
            request.get<crude_json::object>().erase("id");
            Require(session.Handle(request).is_null() && forwarded==1,"Resource notification executed");
        }
        for(const auto& [path,bytes]:unchanged)Require(Read(path)==bytes,"MCP binding changed source/identity metadata bytes");
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: project-registry MCP Model/Script/Audio GUID/path binding, properties/clear/no-op, wrong type/identity/move/missing/root refusal, no execution/file writes and transient/allocator preservation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<"; retained fixture="<<fixture<<'\n';return 1;}
}
