#include "engine/audio/EntityAudioHost.h"
#include "engine/editor/EntityHistory.h"
#include "engine/editor/EntityDocumentState.h"
#include "wave_fixture.h"
#include <iostream>

namespace {void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Scene;using namespace Engine::AudioSystem;
    const auto root=fs::temp_directory_path()/(L"kubo-entity-audio-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();bool owns=false,device=false,com=false;
    try {
        Require(!fs::exists(root),"Fixture collision");fs::create_directories(root/"Assets");owns=true;fs::create_directories(root/"Other");
        const auto wave=root/L"Assets/音声.wav";
        {std::ofstream file(wave,std::ios::binary);const auto bytes=WaveFixture::Pcm(0);file.write(bytes.data(),bytes.size());Require(bool(file),"Wave fixture failed");}
        Engine::Assets::AssetRegistry registry;std::string error;Require(registry.Scan(root/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(wave)->guid;
        EntityWorld world;const auto parent=world.CreateEntity("Parent"),id=world.CreateEntity("Sound",parent);const auto legacy=world.Capture().dump();
        Engine::Editor::EntityHistory history;Engine::Editor::EntityDocumentState document;document.Opened(world,root/"entry.scene");
        AudioComponent settings;settings.assetPath="Assets/音声.wav";settings.assetGuid=guid;settings.loop=true;settings.playOnAwake=true;settings.volume=0;
        const auto before=world.Capture();Require(world.SetAudio(id,settings),"SetAudio rejected valid source");
        Require(history.Record(before,world,id,id,error),error.c_str());history.Commit();document.Observe(world);Require(document.IsDirty(),"Audio edit did not dirty authored world");
        EntityId selection=id;Require(history.Undo(world,selection,error) && !world.Find(id)->hasAudio && world.Capture().dump()==legacy,"Audio undo changed legacy schema or failed");
        Require(history.Redo(world,selection,error) && world.Find(id)->hasAudio && world.Find(id)->audio.assetGuid==guid,"Audio redo lost identity");
        const auto baseline=world.Capture().dump();
        Require(world.Save(root/L"場面.scene",error),error.c_str());EntityWorld loaded;Require(loaded.Load(root/L"場面.scene",error) && loaded.Capture().dump()==baseline,"Audio native Scene roundtrip failed");
        EntityId clone=0;Require(loaded.DuplicateSubtree(id,parent,clone,error) && loaded.Find(clone)->audio.assetGuid==guid && loaded.Find(clone)->audio.volume==0,"Subtree clone lost shared authored Audio");
        for(const auto volume:{-1e-100,1.000000001,2.0}) {
            auto invalid=world.Capture();invalid["entities"][1]["audio"]["volume"]=volume;
            Require(!world.Restore(invalid,error) && world.Capture().dump()==baseline,"Invalid double Audio volume accepted or changed world");
        }
        for(const auto& field:std::vector<std::string>{"enabled","loop","playOnAwake"}) {
            auto invalid=world.Capture();invalid["entities"][1]["audio"][field]=1.0;
            Require(!world.Restore(invalid,error) && world.Capture().dump()==baseline,"Invalid Audio boolean accepted");
        }
        for(const auto& path:std::vector<std::string>{"../outside.wav","C:clip.wav","/clip.wav",std::string("bad\0.wav",8),std::string("\xff",1),std::string(4097,'x')}) {
            auto bad=settings;bad.assetPath=path;Require(!world.SetAudio(id,bad) && world.Capture().dump()==baseline,"Invalid authored path changed Audio");
        }
        auto bad=settings;bad.assetGuid="bogus";Require(!world.SetAudio(id,bad),"Invalid Audio GUID accepted");
        auto invalid=world.Capture();invalid["entities"][1]["audio"]=false;Require(!world.Restore(invalid,error),"Non-object Audio component accepted");
        Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM failed");com=true;Audio::GetInstance()->Initialize(root);device=true;fs::current_path(root/"Other");
        {
            EntityAudioHost host;host.Synchronize(world,registry,root,true);
            Require(host.SourceCount()==1 && host.VoiceCount()==1 && host.BufferBytes()==4 && host.QueuedVoicesHaveVolume(id,0),"Authored GUID Audio did not start owned loop from unrelated cwd");
            auto changed=settings;changed.volume=0.25f;Require(world.SetAudio(id,changed),"Volume authoring failed");host.Synchronize(world,registry,root,true,true);Sleep(30);const auto samples=host.PlayedSamples();Sleep(30);
            Require(host.VoiceCount()==1 && host.QueuedVoicesHaveVolume(id,0.25f) && host.PlayedSamples()==samples,"Paused Audio volume update restarted or consumed samples");
            host.Synchronize(world,registry,root,true,false);Sleep(30);Require(host.PlayedSamples()>samples,"Component Audio did not resume");
            world.Find(parent)->active=false;host.Synchronize(world,registry,root,true);Require(host.VoiceCount()==0 && host.BufferBytes()==0,"Inactive ancestor retained Audio");
            world.Find(parent)->active=true;host.Synchronize(world,registry,root,true);Require(host.VoiceCount()==1,"Reactivation did not initialize source");
            const auto moved=root/L"Assets/移動.wav";fs::rename(wave,moved);fs::rename(Engine::Assets::AssetRegistry::MetadataPath(wave),Engine::Assets::AssetRegistry::MetadataPath(moved));Require(registry.Scan(root/"Assets",error),error.c_str());
            host.Synchronize(world,registry,root,true);Require(host.VoiceCount()==1,"Moved GUID Audio used absent old path");
            {std::ofstream file(wave,std::ios::binary);const auto bytes=WaveFixture::Pcm();file.write(bytes.data(),bytes.size());}
            fs::remove(Engine::Assets::AssetRegistry::MetadataPath(moved));host.Synchronize(world,registry,root,true);
            Require(host.VoiceCount()==0 && host.BufferBytes()==0 && !host.GetLastError(id).empty(),"Missing GUID identity used old-path competitor or kept playing");
            changed.assetGuid.clear();changed.assetPath="Assets/音声.wav";changed.loop=false;changed.volume=0;Require(world.SetAudio(id,changed),"Legacy Audio setting failed");
            host.Synchronize(world,registry,root,true);Sleep(40);Require(host.VoiceCount()==0 && host.SourceCount()==1,"One-shot did not finish");
            for(int frame=0;frame<5;++frame) host.Synchronize(world,registry,root,true);
            Require(host.VoiceCount()==0 && host.BufferBytes()==4,"Finished one-shot replayed on synchronization");
            world.RemoveAudio(id);host.Synchronize(world,registry,root,true);Require(host.SourceCount()==0 && host.BufferBytes()==0,"Removed component retained buffer");
            changed.loop=true;Require(world.SetAudio(id,changed),"Restore component failed");host.Synchronize(world,registry,root,true);host.Synchronize(world,registry,root,false);
            Require(host.SourceCount()==0 && host.VoiceCount()==0 && host.BufferBytes()==0,"Editing/Stop retained component sound");
        }
        Require(!Audio::GetInstance()->IsSoundPlaying(),"Component host leaked voices");Audio::GetInstance()->Finalize();device=false;CoUninitialize();com=false;
        fs::current_path(previous);fs::remove_all(root);owns=false;
        std::cout<<"PASS: authored Audio schema/dirty/history/clone/Unicode save, strict settings, actual GUID Wave playback, volume/pause/ancestor/remove/Stop cleanup, moved/missing identity and one-shot retention\n";return 0;
    } catch(const std::exception& exception) {
        if(device) Audio::GetInstance()->Finalize();if(com) CoUninitialize();fs::current_path(previous);if(owns) {std::error_code cleanup;fs::remove_all(root,cleanup);}
        std::cerr<<exception.what()<<'\n';return 1;
    }
}
