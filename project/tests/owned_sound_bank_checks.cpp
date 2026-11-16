#include "engine/audio/OwnedSoundBank.h"
#include "wave_fixture.h"
#include <iostream>

namespace {
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::AudioSystem;
    const auto root=fs::temp_directory_path()/(L"kubo-owned-sound-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    bool initialized=false,com=false;
    try {
        Require(!fs::exists(root),"Fixture collision");fs::create_directories(root/L"Assets");fs::create_directories(root/L"Elsewhere");
        {std::ofstream file(root/L"Assets/音声.wav",std::ios::binary);const auto bytes=WaveFixture::Pcm(321);file.write(bytes.data(),bytes.size());Require(bool(file),"Cannot write Wave fixture");}
        Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM initialization failed");com=true;
        Audio::GetInstance()->Initialize(root);initialized=true;fs::current_path(root/L"Elsewhere");
        {
            OwnedSoundBank bank,other;std::string error;OwnedSoundBank::Handle first=0,foreign=0;
            Require(bank.Load("Assets/音声.wav",first,error) && first!=0 && bank.BufferBytes()==4,"Owned Unicode load from unrelated cwd failed");
            Require(other.Load("Assets/音声.wav",foreign,error) && foreign!=first,"Cross-owner handle reused");
            Require(!other.Play(first,true,0,error) && !bank.Stop(foreign,error),"Foreign handle accepted");
            for(const auto& path:std::vector<std::string>{"","../Assets/音声.wav","/Assets/音声.wav","C:Assets/音声.wav","Assets/missing.wav",std::string("Assets/x\0.wav",13),std::string("Assets/\xff.wav")}) {
                auto unchanged=first;Require(!bank.Load(path,unchanged,error) && unchanged==first && bank.SoundCount()==1 && bank.BufferBytes()==4,"Rejected load changed bank or output handle");
            }
            Require(!bank.Play(first,true,std::numeric_limits<float>::quiet_NaN(),error) && !bank.Play(first,true,1.1f,error) && bank.VoiceCount()==0,"Invalid volume started voice");
            for(size_t i=0;i<OwnedSoundBank::kMaxVoicesPerSound;++i) Require(bank.Play(first,true,0,error),error.c_str());
            Require(bank.VoiceCount()==8 && !bank.Play(first,true,0,error) && bank.VoiceCount()==8,"Per-sound voice quota failed");
            for(int i=0;i<7;++i) {
                OwnedSoundBank::Handle id=0;Require(bank.Load("Assets/音声.wav",id,error),error.c_str());
                for(int j=0;j<8;++j) Require(bank.Play(id,true,0,error),error.c_str());
            }
            Require(bank.VoiceCount()==64,"Owner voice count failed");
            OwnedSoundBank::Handle extra=0;Require(bank.Load("Assets/音声.wav",extra,error) && !bank.Play(extra,true,0,error),"Total voice quota failed");
            Require(other.Play(foreign,true,0,error) && other.IsPlaying(foreign),"Second owner could not play");
            bank.Clear();Require(bank.SoundCount()==0 && bank.BufferBytes()==0 && bank.VoiceCount()==0 && !bank.IsPlaying(first) && other.IsPlaying(foreign),"Clear stopped foreign owner or retained buffers");
            Require(!bank.Release(first,error) && !bank.Play(first,true,0,error),"Released handle remained usable");
            for(size_t i=0;i<OwnedSoundBank::kMaxSounds;++i) Require(bank.Load("Assets/音声.wav",extra,error),error.c_str());
            const auto old=extra;Require(!bank.Load("Assets/音声.wav",extra,error) && extra==old && bank.SoundCount()==64,"Sound count quota failed");
            bank.Clear();
            {
                std::string data="data";WaveFixture::Put32(data,17*1024*1024);data.append(17*1024*1024,'\0');
                const auto bytes=WaveFixture::Wrap(WaveFixture::Format()+data);
                std::ofstream file(root/L"Assets/large.wav",std::ios::binary);file.write(bytes.data(),bytes.size());Require(bool(file),"Cannot create buffer budget fixture");
            }
            OwnedSoundBank::Handle large=0;
            for(int i=0;i<3;++i) Require(bank.Load("Assets/large.wav",large,error),error.c_str());
            const auto retained=large;
            Require(bank.BufferBytes()==51*1024*1024 && !bank.Load("Assets/large.wav",large,error) && large==retained && bank.SoundCount()==3 && bank.BufferBytes()==51*1024*1024,"Aggregate audio byte quota failed");
            Require(bank.Release(retained,error) && bank.Load("Assets/large.wav",large,error) && large!=retained,"Released buffer budget not recovered or handle reused");
            bank.Clear();
            // Destruction, not an explicit global stop, must retire owned voices.
            {OwnedSoundBank transient;OwnedSoundBank::Handle id=0;Require(transient.Load("Assets/音声.wav",id,error) && transient.Play(id,true,0,error),error.c_str());}
            Require(other.IsPlaying(foreign),"Transient destruction stopped foreign owner");
            Require(other.Release(foreign,error) && !other.IsPlaying(foreign) && other.BufferBytes()==0,"Release did not retire its queued voice");
        }
        Require(!Audio::GetInstance()->IsSoundPlaying(),"Bank destruction left XAudio2 buffers queued");
        Audio::GetInstance()->Finalize();initialized=false;CoUninitialize();com=false;
        fs::current_path(previous);fs::remove_all(root);
        std::cout<<"PASS: actual muted XAudio2 owned buffers, Unicode fixed-root load, unique foreign/stale handles, quotas, selective release/clear/destruction\n";return 0;
    } catch(const std::exception& exception) {
        if(initialized) Audio::GetInstance()->Finalize();if(com) CoUninitialize();
        fs::current_path(previous);std::error_code cleanup;fs::remove_all(root,cleanup);
        std::cerr<<exception.what()<<'\n';return 1;
    }
}
