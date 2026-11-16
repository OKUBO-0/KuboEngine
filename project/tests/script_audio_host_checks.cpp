#include "engine/audio/ScriptAudioHost.h"
#include "engine/scene/entity/WorldPlayback.h"
#include "wave_fixture.h"
#include <iostream>

namespace {
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::AudioSystem;using namespace Engine::Scripting;using namespace Engine::Scene;
    const auto root=fs::temp_directory_path()/(L"kubo-script-audio-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    bool audio=false,com=false;const auto previous=fs::current_path();
    try {
        Require(!fs::exists(root),"Fixture collision");fs::create_directories(root/L"Assets");fs::create_directories(root/"Other");
        {std::ofstream file(root/L"Assets/音声.wav",std::ios::binary);const auto bytes=WaveFixture::Pcm(0);file.write(bytes.data(),bytes.size());Require(bool(file),"Cannot create Wave");}
        const auto write=[&](const std::string& source) {std::ofstream file(root/"audio.lua",std::ios::binary);file<<source;Require(bool(file),"Cannot write Lua");};
        write("function on_create(e) assert(audio.available());audio.play('Assets/音声.wav',true,0) end\nfunction on_update(e,dt) if e:id()==1 then audio.set_volume('Assets/音声.wav',0.25) end end");
        Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM failed");com=true;
        Audio::GetInstance()->Initialize(root);audio=true;fs::current_path(root/"Other");
        {
            ScriptRuntime runtime;ScriptAudioHost host;EntityWorld world;std::string error;
            host.Synchronize(runtime,true);Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());
            const auto first=world.CreateEntity("First"),second=world.CreateEntity("Second");world.SetScript(first,"audio.lua");world.SetScript(second,"audio.lua");
            runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==2 && host.VoiceCount()==2 && host.BufferBytes()==8,"Real Lua commands did not start two owned XAudio2 loops");
            Require(host.QueuedVoicesHaveVolume(first,"Assets/音声.wav",0.25f) && host.QueuedVoicesHaveVolume(second,"Assets/音声.wav",0),"Live volume update did not reach XAudio2 or changed another VM");
            host.Synchronize(runtime,true,true);Sleep(30);const auto samples=host.PlayedSamples();Sleep(30);
            Require(host.IsPaused() && host.PlayedSamples()==samples && host.VoiceCount()==2,"Pause lost queues or continued consuming samples");
            Require(host.QueuedVoicesHaveVolume(first,"Assets/音声.wav",0.25f) && host.QueuedVoicesHaveVolume(second,"Assets/音声.wav",0),"Pause changed voice volume");
            host.Synchronize(runtime,true,false);Sleep(30);Require(!host.IsPaused() && host.PlayedSamples()>samples,"Resume did not consume samples");
            world.Find(first)->script.enabled=false;runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==1 && host.VoiceCount()==1 && host.BufferBytes()==4,"Disabled VM retained voice or stopped healthy owner");
            Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==1 && host.VoiceCount()==1 && host.BufferBytes()==4,"Same-path reload retained old epoch or duplicated voice");
            write("function on_update(e,dt) audio.play('Assets/音声.wav',true,0);error('fault after request') end");
            Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==0 && host.VoiceCount()==0 && !runtime.GetLastError(second).empty(),"Faulted VM played pending command or retained old voices");
            write("local ticks=0\nfunction on_update(e,dt) ticks=ticks+1;if ticks==1 then audio.play('Assets/音声.wav',true,0) elseif ticks==2 then audio.stop_all() end end");
            Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());runtime.Update(world,0.1f);host.Synchronize(runtime,true);Require(host.VoiceCount()==1,"Recovered VM did not play");
            runtime.Update(world,0.1f);host.Synchronize(runtime,true);Require(host.VoiceCount()==0 && host.BufferBytes()==4,"stop_all failed or unnecessarily discarded reusable buffer");
            write("function on_update(e,dt) audio.set_volume('Assets/missing.wav',0.4) end");
            Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==0 && host.BufferBytes()==0 && host.VoiceCount()==0 && !host.GetLastError(second).empty() && runtime.GetLastError(second).empty(),"Volume on unknown sound loaded a buffer, allocated an owner or faulted script");
            write("function on_create(e) audio.play('Assets/音声.wav',true,0) end\nfunction on_update(e,dt) audio.play('Assets/missing.wav',true,0) end");
            Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());size_t reports=0;
            const auto report=[&](uint64_t id,const std::string& message) {Require(id==second && !message.empty(),"Wrong audio diagnostic owner");++reports;};
            runtime.Update(world,0.1f);host.Synchronize(runtime,true,false,report);
            runtime.Update(world,0.1f);host.Synchronize(runtime,true,false,report);
            Require(host.VoiceCount()==1 && reports==1 && !host.GetLastError(second).empty() && runtime.GetLastError(second).empty(),"Device/load failure suspended script, broke valid sound or spammed diagnostics");
            world.DestroyEntity(second);runtime.Update(world,0.1f);host.Synchronize(runtime,true);Require(host.OwnerCount()==0 && host.VoiceCount()==0,"Removed Entity retained VM bank");
            // A command queued before retirement must not resurrect a deleted VM.
            world.Find(first)->script.enabled=true;runtime.Update(world,0.1f);runtime.Shutdown(world);host.Synchronize(runtime,true);
            Require(host.OwnerCount()==0 && host.VoiceCount()==0 && host.BufferBytes()==0,"Shutdown/deferred commands resurrected an owner");
            WorldPlayback playback;Engine::Physics::PhysicsWorld physics;
            host.Synchronize(runtime,false);Require(playback.Play(world),"Play failed");host.Synchronize(runtime,true);runtime.Update(world,0.1f);host.Synchronize(runtime,true);
            Require(host.VoiceCount()==1,"Play did not initialize fresh audio VM");playback.Pause();host.Synchronize(runtime,true,true);
            Require(playback.Stop(world,runtime,physics,error),error.c_str());host.Synchronize(runtime,false);
            Require(host.OwnerCount()==0 && host.VoiceCount()==0 && host.BufferBytes()==0 && playback.GetState()==WorldPlayback::State::Editing,"Stop did not restore authored world and release audio");
            {
                OwnedSoundBank controlled;OwnedSoundBank::Handle handle=0;
                Require(controlled.Load("Assets/音声.wav",handle,error) && controlled.Play(handle,true,0,error) && controlled.Play(handle,true,0,error),"Cannot create overlapping silent voices");
                Require(!controlled.SetVolume(handle,-1,error) && !controlled.SetVolume(handle,std::numeric_limits<float>::quiet_NaN(),error) && !controlled.SetVolume(0,0.5f,error) && controlled.QueuedVoicesHaveVolume(handle,0),"Invalid native volume/handle changed queued voices");
                controlled.PauseAll();Require(controlled.SetVolume(handle,0.4f,error) && controlled.QueuedVoicesHaveVolume(handle,0.4f) && controlled.VoiceCount()==2,"Paused overlapping volume update failed");
                controlled.ResumeAll();Require(controlled.QueuedVoicesHaveVolume(handle,0.4f) && controlled.VoiceCount()==2,"Resume reset volume or replaced voices");
            }
            write("function on_create(e) audio.play('Assets/音声.wav',true,0) end\nfunction on_update(e,dt) end");
            ScriptRuntime quota;ScriptAudioHost limited;EntityWorld owners;limited.Synchronize(quota,true);
            Require(quota.LoadAsset("audio.lua",root,error),error.c_str());EntityId last=0;
            for(int i=0;i<65;++i) {last=owners.CreateEntity("Budget owner");owners.SetScript(last,"audio.lua");}
            quota.Update(owners,0.1f);limited.Synchronize(quota,true);
            Require(limited.OwnerCount()==64 && limited.VoiceCount()==64 && limited.BufferBytes()==256 && !limited.GetLastError(last).empty() && quota.GetLastError(last).empty(),"Aggregate owner/voice quota failed or faulted healthy script");
            quota.Shutdown(owners);limited.Synchronize(quota,true);Require(limited.OwnerCount()==0 && limited.VoiceCount()==0,"Quota owner cleanup failed");
            {
                std::string data="data";WaveFixture::Put32(data,17*1024*1024);data.append(17*1024*1024,'\0');
                const auto bytes=WaveFixture::Wrap(WaveFixture::Format()+data);
                std::ofstream file(root/"Assets/large.wav",std::ios::binary);file.write(bytes.data(),bytes.size());Require(bool(file),"Cannot write aggregate budget fixture");
            }
            write("function on_create(e) audio.play('Assets/large.wav',true,0) end\nfunction on_update(e,dt) end");
            Require(quota.LoadAsset("audio.lua",root,error),error.c_str());EntityWorld memory;
            for(int i=0;i<4;++i) {last=memory.CreateEntity("Memory owner");memory.SetScript(last,"audio.lua");}
            quota.Update(memory,0.1f);limited.Synchronize(quota,true);
            Require(limited.BufferBytes()==51*1024*1024 && limited.VoiceCount()==3 && !limited.GetLastError(last).empty(),"Aggregate buffer quota retained rejected sound or damaged earlier voices");
            quota.Shutdown(memory);limited.Synchronize(quota,true);Require(limited.BufferBytes()==0,"Aggregate buffer cleanup failed");
        }
        Require(!Audio::GetInstance()->IsSoundPlaying(),"Native host lifetime leaked voices");
        Audio::GetInstance()->Finalize();audio=false;CoUninitialize();com=false;
        fs::current_path(previous);fs::remove_all(root);
        std::cout<<"PASS: actual muted Lua/XAudio2 play, sample-count pause/resume, selective disable/remove/reload/fault retirement, stop_all, bounded diagnostics, deferred retirement and Play/Stop cleanup\n";return 0;
    } catch(const std::exception& exception) {
        if(audio) Audio::GetInstance()->Finalize();if(com) CoUninitialize();fs::current_path(previous);std::error_code cleanup;fs::remove_all(root,cleanup);
        std::cerr<<exception.what()<<'\n';return 1;
    }
}
