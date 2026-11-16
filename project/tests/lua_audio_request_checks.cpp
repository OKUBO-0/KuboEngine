#include "engine/script/ScriptRuntime.h"
#include <Windows.h>
#include <fstream>
#include <iostream>

namespace {
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Scripting;using namespace Engine::Scene;
    const auto root=fs::temp_directory_path()/(L"kubo-lua-audio-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(!fs::exists(root),"Fixture collision");fs::create_directories(root);
        struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{root};
        const auto write=[&](const std::string& source) {std::ofstream file(root/"audio.lua",std::ios::binary);file<<source;Require(bool(file),"Cannot write Lua fixture");};
        write(R"(assert(not audio.available())
function on_create(e) assert(audio.available());audio.play('Assets/音声.wav',true,0) end
function on_update(e,dt) audio.stop_all();audio.play('Assets/音声.wav',false,0.5);audio.set_volume('Assets/音声.wav',0.25);e:translate(1,0,0) end
function on_destroy(e) audio.stop_all() end)");
        ScriptRuntime runtime;runtime.SetAudioRequestsEnabled(true);std::string error;
        Require(runtime.LoadAsset("audio.lua",root,error),error.c_str());
        EntityWorld world;auto id=world.CreateEntity("Sound owner");world.SetScript(id,"audio.lua");runtime.Update(world,0.1f);
        auto requests=runtime.TakeAudioRequests();
        Require(requests.size()==4 && requests[0].entity==id && requests[0].path=="Assets/音声.wav" && requests[0].loop && requests[0].volume==0 && requests[1].operation==ScriptAudioRequest::Operation::StopAll && requests[2].volume==0.5f && requests[3].operation==ScriptAudioRequest::Operation::SetVolume && requests[3].volume==0.25f && world.Find(id)->transform.position.x==1,"Successful hook order/owner/path/defaults were lost");
        Require(runtime.TakeAudioRequests().empty(),"Draining replayed audio requests");
        runtime.Update(world,0.1f);runtime.Shutdown(world);Require(runtime.TakeAudioRequests().empty(),"Shutdown retained requests");
        for(const auto& body:std::vector<std::string>{
            "audio.play('Assets/音声.wav');e:translate(9,0,0);error('after request')",
            "audio.play('Assets/音声.wav');while true do end",
            "audio.play('Assets/音声.wav');local x=string.rep('x',16*1024*1024)",
            "for i=1,33 do audio.play('Assets/音声.wav') end",
            "audio.play('../outside.wav')","audio.play('/absolute.wav')","audio.play('C:relative.wav')",
            "audio.play('Assets/a.wav',1)","audio.play('Assets/a.wav',false,2)",
            "audio.play('Assets/a.wav',false,0/0)","audio.play('Assets/a'..string.char(0)..'.wav')",
            "audio.set_volume('Assets/a.wav',0.25);e:translate(9,0,0);error('after volume')",
            "audio.set_volume('Assets/a.wav',-1)","audio.set_volume('Assets/a.wav',2)",
            "audio.set_volume('Assets/a.wav',0/0)","audio.set_volume('Assets/a.wav',true)",
            "audio.set_volume('Assets/a.wav',-1e-100)","audio.set_volume('Assets/a.wav',1.000000001)",
            "audio.play('Assets/a.wav',false,-1e-100)","audio.play('Assets/a.wav',false,1.000000001)",
            "audio.set_volume('Assets/a.wav')","audio.set_volume('../outside.wav',0.5)"}) {
            write("function on_update(e,dt) "+body+" end");
            ScriptRuntime failing;failing.SetAudioRequestsEnabled(true);Require(failing.LoadAsset("audio.lua",root,error),error.c_str());
            EntityWorld entities;const auto failed=entities.CreateEntity("Failed owner");entities.SetScript(failed,"audio.lua");
            failing.Update(entities,0.1f);
            Require(!failing.GetLastError(failed).empty() && failing.TakeAudioRequests().empty() && entities.Find(failed)->transform.position.x==0,"Failed hook published audio or Entity mutation");
        }
        write("function on_update(e,dt) e:translate(1,0,0);for i=1,32 do audio.play('Assets/a.wav') end end");
        ScriptRuntime bounded;bounded.SetAudioRequestsEnabled(true);Require(bounded.LoadAsset("audio.lua",root,error),error.c_str());
        EntityWorld many;std::vector<EntityId> ids;
        for(int i=0;i<9;++i) {const auto owner=many.CreateEntity("Owner");many.SetScript(owner,"audio.lua");ids.push_back(owner);}
        bounded.Update(many,0.1f);requests=bounded.TakeAudioRequests();
        Require(requests.size()==256 && many.Find(ids.back())->transform.position.x==0 && !bounded.GetLastError(ids.back()).empty(),"Queue capacity admitted partial batch or Entity changes");
        for(size_t i=0;i<8;++i) Require(many.Find(ids[i])->transform.position.x==1 && requests[i*32].entity==ids[i],"Queue overflow damaged an earlier successful owner");
        write("function on_update(e,dt) assert(not audio.available()) end");
        ScriptRuntime disabled;Require(disabled.LoadAsset("audio.lua",root,error),error.c_str());
        EntityWorld noHost;const auto off=noHost.CreateEntity("No host");noHost.SetScript(off,"audio.lua");disabled.Update(noHost,0.1f);
        Require(disabled.GetLastError(off).empty() && disabled.TakeAudioRequests().empty(),"Disabled host availability contract failed");
        write("audio.play('Assets/a.wav');function on_update(e,dt) end");
        Require(!bounded.LoadAsset("audio.lua",root,error),"Module initializer published audio");
        bounded.Update(many,0.1f);
        bounded.SetAudioRequestsEnabled(false);Require(bounded.TakeAudioRequests().empty(),"Disabling retained audio requests");
        std::cout<<"PASS: Lua audio command staging, hook success/fault/quota isolation, owner/order/Unicode validation, atomic queue limits, disabled host and shutdown drain\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
