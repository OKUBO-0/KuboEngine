#pragma once

#include "engine/scene/entity/EntityWorld.h"
#include "wave_fixture.h"
#include <Windows.h>
#include <fstream>
#include <stdexcept>

class RuntimeSceneTransitionFixture {
public:
    std::filesystem::path root;

    ~RuntimeSceneTransitionFixture() {
        if (!root.empty()) {
            std::error_code error;
            std::filesystem::remove_all(root,error);
        }
    }

    void Prepare() {
        const auto directory=std::filesystem::temp_directory_path()/
            (L"kubo-scene-transition-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Transition fixture already exists");
        root=directory;
        std::filesystem::create_directory(root/"Assets");
        Write(root/"entry.kubo",R"({"name":"Scene Transition","startupScene":"Assets/entry.scene","assetDirectory":"Assets"})");
        Write(root/"Assets/trigger.lua",R"(
function on_trigger_enter(e,id)
    assert(id==scene.find("Visitor"))
    assert(scene.change_scene("移動先.scene",0.15))
    audio.play("Assets/案内.wav",false,0)
end
)");
        Write(root/"Assets/move.lua",R"(
local step=0
function on_fixed_update(e,dt)
    step=step+1
    if step==2 then e:set_position(0,0,0) end
end
)");
        Write(root/"Assets/arrived.lua",R"(
function on_create(e) e:set_position(0,0,123) end
function on_update(e,dt) end
)");
        std::string pcm="data";
        WaveFixture::Put32(pcm,16000);
        pcm.append(16000,'\0');
        Write(root/L"Assets/案内.wav",WaveFixture::Wrap(WaveFixture::Format()+pcm));
        using Engine::Scene::EntityWorld;
        EntityWorld start;
        const auto sensor=start.CreateEntity("Sensor"),visitor=start.CreateEntity("Visitor");
        Engine::Physics::ColliderComponent trigger,ball;
        trigger.shape=Engine::Physics::ColliderShape::Capsule;
        trigger.halfSegment=1;
        trigger.isTrigger=true;
        ball.shape=Engine::Physics::ColliderShape::Sphere;
        if (!start.SetCollider(sensor,trigger) || !start.SetScript(sensor,"Assets/trigger.lua") ||
            !start.SetCollider(visitor,ball) || !start.SetScript(visitor,"Assets/move.lua"))
            throw std::runtime_error("Cannot author Trigger fixture");
        start.Find(visitor)->transform.position={10,0,0};
        EntityWorld destination;
        if (!destination.SetScript(destination.CreateEntity("Arrived"),"Assets/arrived.lua"))
            throw std::runtime_error("Cannot author destination fixture");
        std::string error;
        if (!start.Save(root/"Assets/entry.scene",error) ||
            !destination.Save(root/L"Assets/移動先.scene",error)) throw std::runtime_error(error);
    }

    bool Arrived(const Engine::Scene::EntityWorld& world) const {
        const auto* entity=world.Find(1);
        return entity && entity->name=="Arrived" && entity->transform.position.z==123 &&
            world.GetEntities().size()==1;
    }

private:
    static void Write(const std::filesystem::path& path,const std::string& bytes) {
        std::ofstream stream(path,std::ios::binary);
        stream<<bytes;
        stream.close();
        if (stream.fail()) throw std::runtime_error("Could not write transition fixture");
    }
};
