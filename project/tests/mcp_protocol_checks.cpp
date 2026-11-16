#include "../engine/services/McpProtocolSession.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int Code(const Engine::Services::McpProtocolSession::Value& response) {
    return response.contains("error") ? static_cast<int>(response["error"]["code"].get<crude_json::number>()) : 0;
}
int main()
{
    using Session = Engine::Services::McpProtocolSession;
    using Value = Session::Value;
    try {
        int forwarded = 0;
        auto time = std::chrono::steady_clock::time_point{};
        Session session([&](const Value& request) {
            ++forwarded;
            Value reply;
            reply["jsonrpc"] = "2.0"; reply["id"] = request["id"];
            if (request["method"].get<crude_json::string>() == "scene.list_entities") reply["result"] = crude_json::array{};
            else reply["result"]["entityId"] = 1.0;
            return reply;
        }, [&]() { return time; });
        auto handle = [&](const char* json) { return session.HandleText(json); };
        Require(Code(handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")) == -32002, "Uninitialized tool access allowed");
        Require(handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").is_null() && session.GetState() == Session::State::New,
            "Early notification bypassed initialize");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})")) == -32602, "Invalid initialization accepted");
        Require(Code(handle(R"({"jsonrpc":"1.0","id":1,"method":"initialize"})")) == -32600, "Invalid JSON-RPC version accepted");
        Require(Code(handle("{")) == -32700, "Invalid JSON was not a parse error");
        Require(Code(handle("[]")) == -32600, "Batch request accepted");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":null,"method":"ping"})")) == -32600, "Null request id accepted");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":1.5,"method":"ping"})")) == -32600, "Fractional id accepted");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":9007199254740992,"method":"ping"})")) == -32600, "Unsafe numeric id accepted");
        Require(handle(R"({"jsonrpc":"2.0","id":"ping","method":"ping"})")["id"].get<crude_json::string>() == "ping", "String id was not preserved");
        const auto initialized = handle(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2099-01-01","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        Require(initialized["result"]["protocolVersion"].get<crude_json::string>() == "2025-11-25", "Unsupported version did not negotiate a supported fallback");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})")) == -32002, "Tools accepted before initialized notification");
        Require(handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").is_null(), "Notification got a response");
        const auto tools = handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})");
        Require(tools["result"]["tools"].get<crude_json::array>().size() == 8 &&
            tools["result"]["tools"][2]["inputSchema"]["required"].is_array() &&
            tools["result"]["tools"][7]["name"].get<crude_json::string>() == "editor_get_status" &&
            tools["result"]["tools"][7]["annotations"]["readOnlyHint"].get<bool>(), "Tool schemas missing");
        Require(handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"editor_get_status","arguments":{"unexpected":true}}})")["result"]["isError"].get<bool>() &&
            forwarded == 0, "Status tool accepted unsupported arguments");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":5,"method":"scene.create_entity"})")) == -32601 && forwarded == 0,
            "Raw Editor method bypassed tool validation");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scene_unknown"}})")) == -32602, "Unknown tool returned wrong error");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"scene_create_entity","arguments":[]}})")) == -32602, "Malformed tool call accepted");
        const auto invalidArgs = handle(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"scene_create_entity","arguments":{"name":123}}})");
        Require(invalidArgs["result"]["isError"].get<crude_json::boolean>() && forwarded == 0, "Schema error mutated Editor or used a protocol error");
        Require(handle(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"scene_create_entity"}})").is_null() && forwarded == 0,
            "Tool notification caused a mutation");
        const auto modern = handle(R"({"jsonrpc":"2.0","id":"list","method":"tools/call","params":{"name":"scene_list_entities"}})");
        Require(modern["result"]["structuredContent"]["entities"].is_array() && forwarded == 1, "Modern structured result missing");
        for (int i = 0; i < 29; ++i) {
            Value call;
            call["jsonrpc"] = "2.0"; call["id"] = double(20 + i);
            call["method"] = "tools/call"; call["params"]["name"] = "scene_list_entities";
            session.Handle(call);
        }
        Require(handle(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"scene_list_entities"}})")["result"]["isError"].get<crude_json::boolean>() && forwarded == 30,
            "Rate limit did not protect the handler");
        time += std::chrono::seconds(1);
        handle(R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"scene_list_entities"}})");
        Require(forwarded == 31, "Rate limiter did not reset");
        Require(Code(handle(R"({"jsonrpc":"2.0","id":11,"method":"initialize"})")) == -32600, "Connection was initialized twice");
        Require(handle(R"({"jsonrpc":"2.0","id":11,"result":{}})").is_null(), "Client response caused a response loop");
        Require(Code(session.HandleText(std::string(65, '[') + std::string(65, ']'))) == -32700, "Deep JSON was not bounded");
        Require(Code(session.HandleText(std::string(1, static_cast<char>(0xff)))) == -32700, "Invalid UTF-8 accepted");
        std::istringstream input(std::string(Session::MaxFrameBytes + 20, 'x') + "\n{}\n");
        std::string line;
        bool oversized = false;
        Require(Session::ReadFrame(input, line, oversized) && oversized && line.size() == Session::MaxFrameBytes, "Frame allocation was not bounded");
        Require(Session::ReadFrame(input, line, oversized) && !oversized && line == "{}", "Oversized frame lost stream synchronization");
        Session failure([](const Value&) -> Value { throw std::runtime_error("Editor unavailable"); });
        failure.HandleText(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        Require(failure.GetProtocolVersion() == "2025-11-25", "Supported modern version was not echoed");
        failure.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        const auto unavailable = failure.HandleText(R"({"jsonrpc":"2.0","id":"io","method":"tools/call","params":{"name":"scene_list_entities"}})");
        Require(unavailable["id"].get<crude_json::string>() == "io" && unavailable["result"]["isError"].get<crude_json::boolean>(),
            "Editor failure was not an execution error with matching id");
        Session old([](const Value&) { Value r; r["result"]["saved"] = true; return r; });
        old.HandleText(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        Require(old.GetProtocolVersion() == "2024-11-05", "Legacy version was not echoed");
        old.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        const auto legacy = old.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene_save","arguments":{"path":"entry.scene"}}})");
        Require(legacy.contains("result") && !legacy["result"].contains("structuredContent"), "Modern-only result leaked into legacy version");
        Value received;int editCalls=0;
        Session edits([&](const Value& request){received=request;++editCalls;Value r;r["result"]["updated"]=true;return r;});
        edits.HandleText(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        edits.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        const auto advertised=edits.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")["result"]["tools"];
        Require(advertised[3]["inputSchema"]["anyOf"].get<crude_json::array>().size()==3 && advertised[3]["inputSchema"]["properties"]["position"]["maxItems"].get<double>()==3 && advertised[4]["annotations"]["destructiveHint"].get<bool>(),"Entity tool schema/annotations missing");
        for(const char* arguments:{R"({})",R"({"entityId":0,"position":[0,0,0]})",R"({"entityId":1.5,"position":[0,0,0]})",R"({"entityId":9007199254740992,"position":[0,0,0]})",R"({"entityId":1})",R"({"entityId":1,"position":[1,2]})",R"({"entityId":1,"position":[true,2,3]})",R"({"entityId":1,"position":[1e100,2,3]})",R"({"entityId":1,"rotation":[0,0,0],"parent":2})"}) {
            Value request;request["jsonrpc"]="2.0";request["id"]=3.0;request["method"]="tools/call";request["params"]["name"]="scene_set_transform";request["params"]["arguments"]=Value::parse(arguments);
            Require(edits.Handle(request)["result"]["isError"].get<bool>() && editCalls==0,"Invalid Transform was forwarded");
        }
        Require(edits.HandleText(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"scene_set_transform","arguments":{"entityId":1,"position":[2,3,4]}}})")["result"]["structuredContent"]["updated"].get<bool>() && received["method"].get<std::string>()=="scene.set_transform" && editCalls==1,"Valid Transform not mapped/structured");
        Require(edits.HandleText(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scene_delete_entity","arguments":{"entityId":1,"position":[0,0,0]}}})")["result"]["isError"].get<bool>() && editCalls==1,"Delete accepted extra arguments");
        edits.HandleText(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"scene_delete_entity","arguments":{"entityId":1}}})");
        Require(editCalls==2 && received["method"].get<std::string>()=="scene.delete_entity","Delete tool not mapped");
        Require(edits.HandleText(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"scene_delete_entity","arguments":{"entityId":1}}})").is_null() && editCalls==2,"Delete notification mutated world");
        const auto beforeParentCalls=editCalls;
        for(const char* arguments:{R"({"entityId":1})",R"({"entityId":1,"parentId":-1})",R"({"entityId":1,"parentId":0.5})",R"({"entityId":1,"parentId":true})",R"({"entityId":1,"parentId":9007199254740992})",R"({"entityId":1,"parentId":0,"keepWorld":1})",R"({"entityId":1,"parentId":0,"unknown":true})"}) {
            Value request;request["jsonrpc"]="2.0";request["id"]=7.0;request["method"]="tools/call";request["params"]["name"]="scene_set_parent";request["params"]["arguments"]=Value::parse(arguments);
            Require(edits.Handle(request)["result"]["isError"].get<bool>() && editCalls==beforeParentCalls,"Invalid parent arguments forwarded");
        }
        edits.HandleText(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"scene_set_parent","arguments":{"entityId":1,"parentId":0}}})");
        Require(editCalls==beforeParentCalls+1 && received["method"].get<std::string>()=="scene.set_parent","Root parent not forwarded");
        Require(edits.HandleText(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"scene_set_parent","arguments":{"entityId":1,"parentId":0}}})").is_null() && editCalls==beforeParentCalls+1,"Parent notification mutated world");
        Require(advertised[5]["inputSchema"]["properties"]["parentId"]["minimum"].get<double>()==0 && advertised[5]["inputSchema"]["required"].get<crude_json::array>().size()==2,"Parent schema missing");
        std::cout << "PASS: MCP lifecycle/version negotiation, notifications, errors, input schemas, rate limit and bounded UTF-8 frames\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
