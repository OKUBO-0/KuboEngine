#include "engine/audio/WaveFile.h"
#include "wave_fixture.h"
#include <iostream>
namespace {
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& bytes) {std::ofstream file(path,std::ios::binary);file.write(bytes.data(),bytes.size());Require(bool(file),"Fixture write failed");}
}
int wmain(int argc,wchar_t** argv) {
    // Optional differential probe for the standalone export script. No device/GPU.
    if(argc==2) {
        Engine::AudioSystem::SoundData data;std::string error;
        const bool accepted=Engine::AudioSystem::WaveFile::Load(argv[1],data,error);
        std::cout<<(accepted?"ACCEPT":"REJECT")<<'\n';return accepted?0:2;
    }
    namespace fs=std::filesystem;using namespace Engine::AudioSystem;using namespace WaveFixture;
    const auto fixture=fs::temp_directory_path()/(L"kubo-wave-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(!fs::exists(fixture),"Fixture collision");fs::create_directories(fixture);
        struct Cleanup {fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{fixture};
        const auto path=fixture/L"音声.wav";SoundData output;std::string error;
        Write(path,Pcm(321));Require(WaveFile::Load(path,output,error) && error.empty() && output.bufferSize==4 && output.wfex.nSamplesPerSec==8000 && output.buffer[0]==65 && output.buffer[1]==1,"Native PCM decode failed");
        const auto baseline=output.buffer;
        const auto reject=[&](const std::string& bytes) {Write(path,bytes);Require(!WaveFile::Load(path,output,error) && !error.empty() && output.buffer==baseline && output.bufferSize==4 && output.wfex.nSamplesPerSec==8000,"Malformed Wave changed output or was accepted");};
        auto bad=Pcm();bad[0]='X';reject(bad);
        bad=Pcm();Set32(bad,4,0xffffffffu);reject(bad);
        bad=Pcm();Set32(bad,16,0xffffffffu);reject(bad);
        bad=Pcm();bad.pop_back();reject(bad);
        bad=Pcm();bad[32]=3;reject(bad);
        reject(Wrap(Format()+Format()+Data()));reject(Wrap(Format()+Data()+Data()));
        reject(Wrap(Data()));reject(Wrap(Format()));reject(Wrap(Format(0xfffe)+Data()));
        reject(Wrap(Format(3,16)+Data()));reject(Wrap(Format(1,12)+Data()));
        reject(Wrap("JUNK"));
        std::string junk="JUNK";Put32(junk,1);junk+="x";reject(Wrap(Format()+Data()+junk));
        junk+='\0';Write(path,Wrap(junk+Data(321)+Format()));
        Require(WaveFile::Load(path,output,error) && output.buffer==baseline,"Odd unknown chunk padding or data-before-format failed");
        auto fmt18=Format();Set32(fmt18,4,18);Put16(fmt18,0);Write(path,Wrap(fmt18+Data(321)));
        Require(WaveFile::Load(path,output,error) && output.wfex.cbSize==0,"18-byte PCM format failed");
        fmt18.back()=1;reject(Wrap(fmt18+Data()));
        for(const uint16_t bits : {8,16,24,32}) {
            std::string data="data";Put32(data,bits/8*2);data+=std::string(bits/8*2,'\0');
            Write(path,Wrap(Format(1,bits)+data));Require(WaveFile::Load(path,output,error) && output.wfex.wBitsPerSample==bits && output.bufferSize==static_cast<unsigned>(bits/8*2),"Supported PCM width failed");
        }
        auto eightChannels=Format();eightChannels[10]=8;eightChannels[20]=16;Set32(eightChannels,16,128000);
        std::string multiData="data";Put32(multiData,32);multiData+=std::string(32,'\0');Write(path,Wrap(eightChannels+multiData));
        Require(WaveFile::Load(path,output,error) && output.wfex.nChannels==8 && output.wfex.nBlockAlign==16,"Eight-channel PCM failed");
        Write(path,Wrap(Format(3,32)+Data()));Require(WaveFile::Load(path,output,error) && output.wfex.wFormatTag==3,"Float32 decode failed");
        Write(path,Pcm());fs::resize_file(path,WaveFile::kMaxBytes+1);
        Require(!WaveFile::Load(path,output,error),"Oversized file accepted");
        Require(!WaveFile::Load(fixture/"missing.wav",output,error) && !WaveFile::Load({},output,error),"Missing/empty file accepted");
        Require(!WaveFile::Load(fixture/fs::path(std::wstring(L"wrong\0suffix",12)),output,error),"NUL path accepted");
        std::cout<<"PASS: bounded native Unicode PCM/float Wave decoding, RIFF/chunk/format/padding validation, data order, unsupported formats and transactional failure\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
