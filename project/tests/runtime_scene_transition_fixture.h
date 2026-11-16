#pragma once

#include "engine/scene/entity/EntityWorld.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/input/InputFrame.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
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

    void Prepare(bool button=false) {
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
        if(button){
            const auto canvas=destination.CreateEntity("HUD Canvas");
            const auto score=destination.CreateEntity("Score",canvas);
            Engine::Scene::SpriteComponent label;
            label.text="得点 0";label.textSize=48;label.size={320,104};
            label.anchor={1,0};label.pivot={1,0};label.textAlignment=Engine::Scene::UITextAlignment::Right;
            label.textColor={1,0,0,1};
            destination.Find(score)->transform.position={-24,20,0};
            if(!destination.SetCanvas(canvas,{}) || !destination.SetSprite(score,label))
                throw std::runtime_error("Could not author text-only HUD");
        }
        if (!start.Save(root/"Assets/entry.scene",error) ||
            !destination.Save(root/L"Assets/移動先.scene",error)) throw std::runtime_error(error);
        if(button){
            const auto image=Engine::Base::ResolveEngineResource("Resources/textures/common/white.png");
            std::filesystem::copy_file(image,root/"Assets/white.png");
            Engine::Assets::AssetRegistry assets;
            if(!assets.Scan(root/"Assets",error))throw std::runtime_error(error);
            const auto* next=assets.FindByPath(root/L"Assets/移動先.scene");
            const auto* entry=assets.FindByPath(root/"Assets/entry.scene");
            if(!next || !entry)throw std::runtime_error("Button Scene assets missing");
            EntityWorld title;
            const auto canvas=title.CreateEntity("Title Canvas");
            const auto covered=title.CreateEntity("Covered Button",canvas),click=title.CreateEntity("Play Button",canvas);
            Engine::Scene::SpriteComponent sprite;
            sprite.assetPath="Assets/white.png";sprite.size={420,240};sprite.anchor={0.5f,0.5f};
            sprite.buttonSceneGuid=entry->guid;
            if(!title.SetCanvas(canvas,{}) || !title.SetSprite(covered,sprite))throw std::runtime_error("Could not create title Canvas");
            sprite.buttonSceneGuid=next->guid;sprite.sortOrder=1;sprite.flipX=true;
            sprite.color={0,0,0.35f,1};sprite.text="ゲーム開始";sprite.textSize=64;sprite.textColor={0,1,0,1};
            if(!title.SetSprite(click,sprite) || !title.Save(root/"Assets/entry.scene",error))throw std::runtime_error(error);
        }
    }

    bool Arrived(const Engine::Scene::EntityWorld& world) const {
        const auto* entity=world.Find(1);
        return entity && entity->name=="Arrived" && entity->transform.position.z==123 &&
            (world.GetEntities().size()==1 || (world.Find(3) && world.Find(3)->sprite.text=="得点 0"));
    }
    static bool ButtonTestOutside(int frame) {return frame==11 || frame==12;}
    static void ButtonInput(Engine::InputSystem::InputFrame& input,bool enabled,int frame) {
        input.available=enabled && frame!=16;
        const bool press=frame==10 || frame==12 || frame==15 || frame==21;
        const bool release=frame==11 || frame==13 || frame==17 || frame==22;
        input.mouse[0]={press,press,release};
    }
    static bool DrawnText(Engine::Base::DirectXCommon& graphics,Engine::Base::OffscreenRenderManager& offscreen,bool hud) {
        auto* color=offscreen.GetViewRenderTarget(false);
        const auto desc=color->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total=0;
        graphics.GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;
        buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if(FAILED(graphics.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))throw std::runtime_error("UI text readback allocation");
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=color;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION copy{};copy.pResource=readback.Get();copy.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        copy.PlacedFootprint=footprint;
        graphics.TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);
        graphics.GetCommandList()->CopyTextureRegion(&copy,0,0,0,&source,nullptr);
        graphics.TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        graphics.CommandKick();
        void* mapped=nullptr;
        D3D12_RANGE range{0,size_t(total)};
        if(FAILED(readback->Map(0,&range,&mapped)))throw std::runtime_error("UI text readback mapping");
        size_t coverage=0;
        const size_t firstX=hud?desc.Width*3/4:desc.Width/3,lastX=hud?desc.Width:desc.Width*2/3;
        const size_t firstY=hud?0:desc.Height/3,lastY=hud?desc.Height/4:desc.Height*2/3;
        for(size_t y=firstY;y<lastY;++y)for(size_t x=firstX;x<lastX;++x){
            const auto* pixel=static_cast<const uint8_t*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
            if(hud ? pixel[0]>220 && pixel[1]<40 && pixel[2]<40 : pixel[1]>220 && pixel[0]<40 && pixel[2]<90)
                ++coverage;
        }
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);
        return coverage>100;
    }

private:
    static void Write(const std::filesystem::path& path,const std::string& bytes) {
        std::ofstream stream(path,std::ios::binary);
        stream<<bytes;
        stream.close();
        if (stream.fail()) throw std::runtime_error("Could not write transition fixture");
    }
};
