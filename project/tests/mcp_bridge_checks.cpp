#include "../engine/services/McpCommandBridge.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <Windows.h>
#include <fstream>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    const auto fixture = std::filesystem::temp_directory_path() /
        ("kubo-mcp-bridge-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    try {
        std::filesystem::create_directories(fixture);
        Engine::Scene::EntityWorld world;
        Engine::Services::McpCommandBridge bridge(fixture);
        const auto created = bridge.Handle(Engine::Scene::SceneValue::parse(R"({"jsonrpc":"2.0","id":1,"method":"scene.create_entity","params":{"name":"RemoteEntity"}})"), world);
        Require(created.contains("result") && world.GetEntities().size() == 1, "MCP create_entity failed");
        const auto listed = bridge.Handle(Engine::Scene::SceneValue::parse(R"({"jsonrpc":"2.0","id":2,"method":"scene.list_entities"})"), world);
        Require(listed.contains("result") && listed["result"].is_array(), "MCP list_entities failed");
        const auto denied = bridge.Handle(Engine::Scene::SceneValue::parse(R"({"jsonrpc":"2.0","id":3,"method":"scene.save","params":{"path":"../outside.json"}})"), world);
        Require(denied.contains("error"), "MCP path traversal was not rejected");
        for (const auto* path : {"C:outside.json", "/outside.json", "Assets/data.json:stream", "Project.kubo", ""}) {
            Engine::Scene::SceneValue request;
            request["id"] = 4.0;
            request["method"] = "scene.save";
            request["params"]["path"] = path;
            Require(bridge.Handle(request, world).contains("error"), "Unsafe scene save path accepted");
        }
        const auto saved = bridge.Handle(Engine::Scene::SceneValue::parse(R"({"id":5,"method":"scene.save","params":{"path":"Assets/Scenes/日本語..scene.json"}})"), world);
        Require(saved.contains("result"), "Safe Unicode filename with dots was rejected");
        auto temporary = fixture / "existing.json.tmp";
        { std::ofstream output(temporary); output << "unrelated temporary"; }
        Require(bridge.Handle(Engine::Scene::SceneValue::parse(R"({"id":6,"method":"scene.save","params":{"path":"existing.json"}})"), world).contains("result"), "Unrelated legacy temporary blocked exclusive unique-temp save");
        std::ifstream input(temporary);
        std::string text;
        std::getline(input, text);
        Require(text == "unrelated temporary", "Successful save changed unrelated temporary data");
        input.close();
        Engine::Scene::EntityWorld persisted;std::string error;
        Require(persisted.Load(fixture/"existing.json",error) && persisted.GetEntities().size()==1 && persisted.GetEntities()[0].name=="RemoteEntity", "MCP save did not persist the live world beside an unrelated temporary");
        const auto parent=world.CreateEntity("Parent");const auto child=world.CreateEntity("Child",parent);
        world.Find(child)->transform.position={4,5,6};
        Engine::Scene::SceneValue edit;edit["id"]=8.0;edit["method"]="scene.set_transform";edit["params"]["entityId"]=double(parent);edit["params"]["position"]=crude_json::array{2.0,3.0,4.0};
        Require(bridge.Handle(edit,world).contains("result") && world.Find(parent)->transform.position.x==2 && world.Find(parent)->transform.scale.x==1,"Local partial Transform failed");
        const auto unchanged=world.Capture().dump();
        edit["params"]["rotation"]=crude_json::array{0.0,1e100,0.0};
        Require(bridge.Handle(edit,world).contains("error") && world.Capture().dump()==unchanged,"Partial invalid Transform committed position");
        edit["params"]["rotation"]=crude_json::array{0.0,0.0,0.0};edit["params"]["entityId"]=9007199254740992.0;
        Require(bridge.Handle(edit,world).contains("error") && world.Capture().dump()==unchanged,"Unsafe Entity ID accepted");
        edit["params"]["entityId"]=double(parent);edit["params"]["scale"]=crude_json::array{-1.0,0.0,2.0};
        Require(bridge.Handle(edit,world).contains("result") && world.Find(parent)->transform.scale.x==-1 && world.Find(parent)->transform.scale.y==0,"Authored negative/zero scale rejected");
        Engine::Scene::SceneValue remove;remove["id"]=9.0;remove["method"]="scene.delete_entity";remove["params"]["entityId"]=double(parent);
        Require(bridge.Handle(remove,world).contains("result") && !world.Find(parent) && world.Find(child)->parent==0 && world.Find(child)->transform.position.x==4,"Delete did not retain/root children with local TRS");
        const auto afterDelete=world.Capture().dump();
        Require(bridge.Handle(remove,world).contains("error") && world.Capture().dump()==afterDelete,"Missing Entity delete mutated world");
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: JSON-RPC entity commands, project-relative path guard and unique-temp save preserving unrelated legacy temporary\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::filesystem::remove_all(fixture);
        return 1;
    }
}
