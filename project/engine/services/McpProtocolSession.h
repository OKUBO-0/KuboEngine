#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../scene/core/SceneDocument.h"
#include "McpEntityArguments.h"
#include "McpComponentArguments.h"
#include <chrono>
#include <cmath>
#include <functional>
#include <istream>
#include <exception>
#include <utility>

namespace Engine::Services {
class McpProtocolSession final {
public:
    using Value = Scene::SceneValue;
    using Handler = std::function<Value(const Value&)>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    enum class State { New, Negotiated, Ready };
    static constexpr size_t MaxFrameBytes = 65536;
    explicit McpProtocolSession(Handler handler, Clock clock = [] { return std::chrono::steady_clock::now(); })
        : handler_(std::move(handler)), clock_(std::move(clock)) {}
    State GetState() const { return state_; }
    const std::string& GetProtocolVersion() const { return version_; }

    // Drain oversized frames without allocating unbounded input, preserving
    // synchronization with the next newline-delimited request.
    static bool ReadFrame(std::istream& input, std::string& line, bool& oversized)
    {
        line.clear(); oversized = false;
        bool consumed = false;
        char ch = 0;
        while (input.get(ch)) {
            consumed = true;
            if (ch == '\n') return true;
            if (line.size() < MaxFrameBytes) line += ch;
            else oversized = true;
        }
        return consumed;
    }

    Value HandleText(const std::string& text)
    {
        if (text.size() > MaxFrameBytes) return Error(nullptr, -32700, "MCP frame exceeds 64 KiB");
        if (!text.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0))
            return Error(nullptr, -32700, "MCP input must be valid UTF-8");
        // Protect the legacy recursive JSON parser before invoking it.
        int depth = 0;
        bool quoted = false, escaped = false;
        for (const auto ch : text) {
            if (quoted) {
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') quoted = false;
            } else if (ch == '"') quoted = true;
            else if (ch == '{' || ch == '[') { if (++depth > 64) return Error(nullptr, -32700, "MCP JSON nesting exceeds 64 levels"); }
            else if (ch == '}' || ch == ']') --depth;
        }
        try {
            const auto request = Value::parse(text);
            if (request.is_discarded()) return Error(nullptr, -32700, "Invalid JSON");
            return Handle(request);
        } catch (const std::exception& e) { return Error(nullptr, -32603, e.what()); }
    }

    Value Handle(const Value& request)
    {
        if (!request.is_object()) return Error(nullptr, -32600, "Expected one JSON-RPC request object (batch requests unsupported)");
        if (!request.contains("method") && (request.contains("result") || request.contains("error"))) return nullptr;
        const bool notification = !request.contains("id") && request.contains("method") && request["method"].is_string();
        const auto id = request.contains("id") && ValidId(request["id"]) ? request["id"] : Value(nullptr);
        if (!request.contains("jsonrpc") || !request["jsonrpc"].is_string() || request["jsonrpc"].get<crude_json::string>() != "2.0" ||
            !request.contains("method") || !request["method"].is_string() || request.contains("result") || request.contains("error") ||
            (!notification && id.is_null())) return notification ? Value(nullptr) : Error(id, -32600, "Invalid JSON-RPC request");
        const auto method = request["method"].get<crude_json::string>();
        if (request.contains("params") && !request["params"].is_object()) return notification ? Value(nullptr) : Error(id, -32602, "MCP params must be an object");
        if (notification) {
            if (method == "notifications/initialized" && state_ == State::Negotiated) state_ = State::Ready;
            return nullptr; // Notifications never invoke Editor mutations.
        }
        const Value params = request.contains("params") ? request["params"] : Value(crude_json::object{});
        if (method == "ping") return Response(id, crude_json::object{});
        if (method == "initialize") {
            if (state_ != State::New) return Error(id, -32600, "This connection is already initialized");
            if (!StringField(params, "protocolVersion") || params["protocolVersion"].get<crude_json::string>().empty() ||
                !params.contains("capabilities") || !params["capabilities"].is_object() || !params.contains("clientInfo") || !params["clientInfo"].is_object() ||
                !StringField(params["clientInfo"], "name") || !StringField(params["clientInfo"], "version"))
                return Error(id, -32602, "initialize requires protocolVersion, capabilities and clientInfo.name/version");
            const auto requested = params["protocolVersion"].get<crude_json::string>();
            version_ = requested == "2024-11-05" ? requested : "2025-11-25";
            Value result;
            result["protocolVersion"] = version_;
            result["capabilities"]["tools"]["listChanged"] = false;
            result["serverInfo"]["name"] = "KuboEngine";
            result["serverInfo"]["version"] = "0.1.0";
            result["instructions"] = "Tools target the opened, explicitly enabled Editor project. Saving replaces the selected scene file. Stop playback before modifying scenes.";
            state_ = State::Negotiated;
            return Response(id, std::move(result));
        }
        if (state_ != State::Ready) return Error(id, -32002, "Complete initialize and notifications/initialized before using tools");
        if (method == "tools/list") {
            if (params.contains("cursor")) return Error(id, -32602, "Invalid cursor: the tool list fits in one page");
            Value result;
            result["tools"] = crude_json::array{};
            result["tools"].push_back(Tool("scene_list_entities", "List entities in the opened Editor scene", {}, true, false));
            result["tools"].push_back(Tool("scene_create_entity", "Create an entity in the opened Editor scene", "name", false, false));
            auto save = Tool("scene_save", "Save the opened scene to a project-relative .scene/.json path, replacing that scene file", "path", false, true);
            save["inputSchema"]["required"] = crude_json::array{"path"};
            result["tools"].push_back(std::move(save));
            result["tools"].push_back(EntityTool(true));
            result["tools"].push_back(EntityTool(false));
            auto parent=Tool("scene_set_parent","Set parentId (0=root); keepWorld boolean optionally preserves World pose or rejects unrepresentable TRS; default retains local TRS; one undo command",{},false,true);
            parent["inputSchema"]["required"]=crude_json::array{"entityId","parentId"};
            parent["inputSchema"]["properties"]["keepWorld"]["type"]="boolean";
            parent["inputSchema"]["properties"]["keepWorld"]["default"]=false;
            for(const char* field:{"entityId","parentId"}) {
                auto& property=parent["inputSchema"]["properties"][field];property["type"]="integer";
                property["minimum"]=std::string(field)=="entityId"?1.0:0.0;property["maximum"]=9007199254740991.0;
            }
            result["tools"].push_back(std::move(parent));
            auto component=Tool("scene_set_component","Patch Camera/Collider/RigidBody/Model/Script/Audio/Light/Material/Canvas/Sprite; omitted fields preserved, null removes; asset paths are project-relative, GUID authoritative (guid:null clears); Sprite asset uses canonical forward slashes; Script properties replace the complete override map; active registry/type/source and coupled consistency validated by Editor; one undo command, does not import assets or execute scripts",{},false,true);
            component["inputSchema"]=McpComponentToolSchema();
            result["tools"].push_back(std::move(component));
            result["tools"].push_back(Tool("editor_get_status",
                "Read the current Editor Scene, playback, authoring lock, selection, and Scene overlay status", {}, true, false));
            return Response(id, std::move(result));
        }
        if (method != "tools/call") return Error(id, -32601, "Method not found");
        if (!StringField(params, "name") || (params.contains("arguments") && !params["arguments"].is_object()) || params.contains("task"))
            return Error(id, -32602, "tools/call requires a tool name and optional object arguments; tasks are unsupported");
        const auto name = params["name"].get<crude_json::string>();
        std::string command, property;
        if (name == "scene_list_entities") command = "scene.list_entities";
        else if (name == "scene_create_entity") { command = "scene.create_entity"; property = "name"; }
        else if (name == "scene_save") { command = "scene.save"; property = "path"; }
        else if (name == "scene_set_transform") command = "scene.set_transform";
        else if (name == "scene_delete_entity") command = "scene.delete_entity";
        else if (name == "scene_set_parent") command = "scene.set_parent";
        else if (name == "scene_set_component") command = "scene.set_component";
        else if (name == "editor_get_status") command = "editor.get_status";
        else return Error(id, -32602, "Unknown tool: " + name);
        const Value arguments = params.contains("arguments") ? params["arguments"] : Value(crude_json::object{});
        if(name=="scene_set_component") {
            std::string error;if(!ValidateMcpComponentArguments(arguments,error))return ToolError(id,error);
        } else if(name=="scene_set_parent") {
            std::string error;if(!ValidateMcpParentArguments(arguments,error)) return ToolError(id,error);
        } else if(name=="scene_set_transform" || name=="scene_delete_entity") {
            std::string error;
            if(!ValidateMcpEntityArguments(arguments,name=="scene_set_transform",error)) return ToolError(id,error);
        } else for (const auto& [key, field] : arguments.get<crude_json::object>()) {
            if (key != property || !field.is_string()) return ToolError(id, "Arguments do not match the advertised tool input schema");
        }
        if (name == "scene_save" && (!StringField(arguments, "path") || arguments["path"].get<crude_json::string>().empty())) return ToolError(id, "scene_save requires a non-empty path");
        const auto now = clock_();
        if (now - rateWindow_ >= std::chrono::seconds(1)) { rateWindow_ = now; callsInWindow_ = 0; }
        if (callsInWindow_ >= 30) return ToolError(id, "Tool rate limit reached (30 calls/second per adapter); retry later");
        ++callsInWindow_;
        Value forwarded;
        forwarded["jsonrpc"] = "2.0";
        forwarded["id"] = id;
        forwarded["method"] = command;
        forwarded["params"] = arguments;
        try {
            const auto reply = handler_(forwarded);
            if (!reply.is_object() || (!reply.contains("result") && !reply.contains("error"))) return ToolError(id, "Invalid Editor response");
            Value result;
            result["content"] = crude_json::array{};
            Value text;
            text["type"] = "text";
            text["text"] = reply.dump(); // Preserve existing callers' JSON text payload.
            result["content"].push_back(std::move(text));
            result["isError"] = reply.contains("error");
            if (!reply.contains("error") && version_ == "2025-11-25") {
                if (command == "scene.list_entities") result["structuredContent"]["entities"] = reply["result"];
                else if (reply["result"].is_object()) result["structuredContent"] = reply["result"];
            }
            return Response(id, std::move(result));
        } catch (const std::exception& e) { return ToolError(id, e.what()); }
    }
    static Value Error(Value id, int code, const std::string& message)
    {
        Value response;
        response["jsonrpc"] = "2.0"; response["id"] = std::move(id);
        response["error"]["code"] = double(code); response["error"]["message"] = message;
        return response;
    }
private:
    static bool ValidId(const Value& id) {
        return id.is_string() || (id.is_number() && std::isfinite(id.get<crude_json::number>()) &&
            std::floor(id.get<crude_json::number>()) == id.get<crude_json::number>() && std::abs(id.get<crude_json::number>()) <= 9007199254740991.0);
    }
    static bool StringField(const Value& object, const char* field) { return object.is_object() && object.contains(field) && object[field].is_string(); }
    static Value Response(Value id, Value result) {
        Value response; response["jsonrpc"] = "2.0"; response["id"] = std::move(id); response["result"] = std::move(result); return response;
    }
    static Value ToolError(const Value& id, const std::string& message) {
        Value result, text;
        result["isError"] = true; result["content"] = crude_json::array{};
        text["type"] = "text"; text["text"] = message; result["content"].push_back(std::move(text));
        return Response(id, std::move(result));
    }
    static Value Tool(const char* name, const char* description, const std::string& property, bool readOnly, bool destructive) {
        Value tool;
        tool["name"] = name; tool["description"] = description;
        tool["inputSchema"]["type"] = "object"; tool["inputSchema"]["properties"] = crude_json::object{};
        tool["inputSchema"]["additionalProperties"] = false;
        if (!property.empty()) tool["inputSchema"]["properties"][property]["type"] = "string";
        tool["annotations"]["readOnlyHint"] = readOnly; tool["annotations"]["destructiveHint"] = destructive;
        tool["annotations"]["openWorldHint"] = false;
        return tool;
    }
    static Value EntityTool(bool transform) {
        auto tool=Tool(transform?"scene_set_transform":"scene_delete_entity",
            transform?"Set local position/rotation (radians)/scale; omitted vectors are preserved; one undo command":"Delete one Entity; direct children become roots retaining local TRS (not subtree deletion); undoable",
            {},false,true);
        auto& schema=tool["inputSchema"];
        schema["required"]=crude_json::array{"entityId"};
        auto& id=schema["properties"]["entityId"];
        id["type"]="integer";id["minimum"]=1.0;id["maximum"]=9007199254740991.0;
        if(transform) {
            schema["anyOf"]=crude_json::array{};
            for(const char* field:{"position","rotation","scale"}) {
                auto& vector=schema["properties"][field];vector["type"]="array";vector["minItems"]=3.0;vector["maxItems"]=3.0;
                vector["items"]["type"]="number";vector["items"]["minimum"]=-double(std::numeric_limits<float>::max());vector["items"]["maximum"]=double(std::numeric_limits<float>::max());
                Value required;required["required"]=crude_json::array{field};schema["anyOf"].push_back(std::move(required));
            }
        }
        return tool;
    }
    Handler handler_;
    Clock clock_;
    State state_ = State::New;
    std::string version_;
    std::chrono::steady_clock::time_point rateWindow_{};
    int callsInWindow_ = 0;
};
}
